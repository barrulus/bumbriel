// Deterministic shm workload. Build with scene_matrix.py --build-client.
#define _GNU_SOURCE
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct buffer {
  struct wl_buffer* object;
  uint32_t* pixels;
  bool busy, retired;
  size_t size;
  struct buffer* next;
};
static struct wl_display* display;
static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm;
static struct wl_surface* surface;
static struct wl_callback* callback;
static struct buffer* buffers;
static int pending_width, pending_height;
static int width = 640, height = 360;
static bool configured, running = true;
static unsigned frame;
static const char* mode;
static uint64_t next_update;
static uint64_t now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void log_event(const char* event) {
  printf("{\"event\":\"%s\",\"monotonic_ns\":%llu,\"frame\":%u}\n", event, (unsigned long long)now(), frame);
  fflush(stdout);
}
static void free_buffer(struct buffer* b) {
  wl_buffer_destroy(b->object);
  munmap(b->pixels, b->size);
  free(b);
}
static void release(void* data, struct wl_buffer* object) {
  (void)object;
  struct buffer* b = data;
  b->busy = false;
  if (b->retired)
    free_buffer(b);
}
static const struct wl_buffer_listener buffer_listener = {release};
static void draw(void);
static void done(void* data, struct wl_callback* cb, uint32_t ms) {
  (void)data;
  (void)ms;
  wl_callback_destroy(cb);
  callback = NULL;
  log_event("frame_done");
  if (!strcmp(mode, "video"))
    draw();
}
static const struct wl_callback_listener frame_listener = {done};
static void draw(void) {
  if (!configured || callback)
    return;
  struct buffer* b = NULL;
  for (struct buffer* candidate = buffers; candidate; candidate = candidate->next)
    if (!candidate->busy) {
      b = candidate;
      break;
    }
  if (!b)
    return;
  // Every backing buffer holds the same immutable background. Only this small
  // rectangle changes in small mode; rotating shm buffers does not expand damage.
  if (!strcmp(mode, "video")) {
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++)
        b->pixels[y * width + x] =
            0xff000000u | (((x + frame * 3) % 256) << 16) | (((y + frame) % 256) << 8) | ((x + y + frame) % 256);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
  } else {
    for (int y = 24; y < 40 && y < height; y++)
      for (int x = 24; x < 32 && x < width; x++)
        b->pixels[y * width + x] = (frame & 1) ? 0xffffffffu : 0xff203040u;
    wl_surface_damage_buffer(surface, 24, 24, 8, 16);
  }
  if (!frame)
    wl_surface_damage_buffer(surface, 0, 0, width, height);
  b->busy = true;
  wl_surface_attach(surface, b->object, 0, 0);
  callback = wl_surface_frame(surface);
  wl_callback_add_listener(callback, &frame_listener, NULL);
  log_event("commit_request");
  wl_surface_commit(surface);
  wl_display_flush(display);
  frame++;
}
static void ping(void* d, struct xdg_wm_base* w, uint32_t s) {
  (void)d;
  xdg_wm_base_pong(w, s);
}
static const struct xdg_wm_base_listener wm_listener = {ping};
static void registry_global(void* d, struct wl_registry* r, uint32_t id, const char* interface, uint32_t v) {
  (void)d;
  if (!strcmp(interface, "wl_compositor"))
    compositor = wl_registry_bind(r, id, &wl_compositor_interface, v < 4 ? v : 4);
  else if (!strcmp(interface, "wl_shm"))
    shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
  else if (!strcmp(interface, "xdg_wm_base")) {
    wm = wl_registry_bind(r, id, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(wm, &wm_listener, NULL);
  }
}
static void registry_remove(void* d, struct wl_registry* r, uint32_t id) {
  (void)d;
  (void)r;
  (void)id;
}
static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};
static void resize_buffers(void) {
  while (buffers) {
    struct buffer* old = buffers;
    buffers = old->next;
    old->retired = true;
    if (!old->busy)
      free_buffer(old);
  }
  size_t size = (size_t)width * height * 4;
  for (int i = 0; i < 3; i++) {
    struct buffer* b = calloc(1, sizeof(*b));
    if (!b)
      exit(1);
    b->size = size;
    int fd = memfd_create("scene-cost", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size))
      exit(1);
    b->pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->pixels == MAP_FAILED)
      exit(1);
    for (size_t p = 0; p < size / 4; p++)
      b->pixels[p] = 0xff203040u;
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int)size);
    b->object = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(b->object, &buffer_listener, b);
    wl_shm_pool_destroy(pool);
    close(fd);
    b->next = buffers;
    buffers = b;
  }
}
static void configure(void* d, struct xdg_surface* s, uint32_t serial) {
  (void)d;
  xdg_surface_ack_configure(s, serial);
  bool resize =
      !configured || (pending_width > 0 && pending_width != width) || (pending_height > 0 && pending_height != height);
  if (pending_width > 0)
    width = pending_width;
  if (pending_height > 0)
    height = pending_height;
  if (width > 8192 || height > 8192 || width < 1 || height < 1)
    exit(1);
  if (resize)
    resize_buffers();
  configured = true;
  // A configure acknowledgement always gets a matching buffer commit, even
  // when a previous frame callback is held by native resize synchronization.
  if (callback) {
    wl_callback_destroy(callback);
    callback = NULL;
  }
  log_event("configure");
  if (resize)
    wl_surface_damage_buffer(surface, 0, 0, width, height);
  draw();
}
static const struct xdg_surface_listener surface_listener = {configure};
static void top_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* a) {
  (void)d;
  (void)t;
  (void)a;
  pending_width = w;
  pending_height = h;
}
static void top_close(void* d, struct xdg_toplevel* t) {
  (void)d;
  (void)t;
  running = false;
}
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure, .close = top_close};
int main(int argc, char** argv) {
  if (argc != 5) {
    fprintf(stderr, "usage: scene-workload idle|small|video title width height\n");
    return 2;
  }
  mode = argv[1];
  width = atoi(argv[3]);
  height = atoi(argv[4]);
  if ((strcmp(mode, "idle") && strcmp(mode, "small") && strcmp(mode, "video"))
      || width < 40
      || height < 40
      || width > 8192
      || height > 8192)
    return 2;
  display = wl_display_connect(NULL);
  if (!display)
    return 1;
  struct wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!compositor || !shm || !wm)
    return 1;
  surface = wl_compositor_create_surface(compositor);
  struct xdg_surface* xdg = xdg_wm_base_get_xdg_surface(wm, surface);
  xdg_surface_add_listener(xdg, &surface_listener, NULL);
  struct xdg_toplevel* top = xdg_surface_get_toplevel(xdg);
  xdg_toplevel_add_listener(top, &top_listener, NULL);
  xdg_toplevel_set_title(top, argv[2]);
  xdg_toplevel_set_app_id(top, "umbriel-scene-cost");
  wl_surface_commit(surface);
  next_update = now() + 500000000;
  while (running) {
    while (wl_display_prepare_read(display) != 0)
      if (wl_display_dispatch_pending(display) < 0)
        return 1;
    wl_display_flush(display);
    int timeout = -1;
    if (!strcmp(mode, "small")) {
      uint64_t t = now();
      timeout = t >= next_update ? 0 : (int)((next_update - t + 999999) / 1000000);
    }
    struct pollfd p = {wl_display_get_fd(display), POLLIN, 0};
    int result = poll(&p, 1, timeout);
    if (result > 0 && p.revents & POLLIN) {
      if (wl_display_read_events(display) < 0)
        break;
    } else
      wl_display_cancel_read(display);
    if (result < 0 && errno != EINTR)
      break;
    if (wl_display_dispatch_pending(display) < 0)
      break;
    if (!strcmp(mode, "small") && now() >= next_update) {
      draw();
      next_update = now() + 500000000;
    }
  }
  wl_display_disconnect(display);
  return 0;
}
