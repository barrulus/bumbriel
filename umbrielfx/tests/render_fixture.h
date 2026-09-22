#pragma once

#include "render/fx_renderer/fx_renderer.h"

#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <umbrielfx/render/fx_renderer/fx_renderer.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <xf86drm.h>

#define TEST_WIDTH 16
#define TEST_HEIGHT 16

struct fixture {
  struct wl_display* display;
  struct wlr_backend* backend;
  struct wlr_renderer* renderer;
  struct wlr_allocator* allocator;
  struct wlr_output* output;
  int drm_fd;
};

static bool check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
  }
  return condition;
}

static bool fixture_try_device(struct fixture* fixture, const char* path) {
  int drm_fd = open(path, O_RDWR | O_CLOEXEC);
  if (drm_fd < 0) {
    return false;
  }

  struct wlr_renderer* renderer = fx_renderer_create_with_drm_fd(drm_fd);
  if (renderer == NULL || !renderer->features.output_color_transform) {
    if (renderer != NULL) {
      wlr_renderer_destroy(renderer);
    }
    close(drm_fd);
    return false;
  }

  fixture->backend->buffer_caps |= WLR_BUFFER_CAP_DMABUF;
  struct wlr_allocator* allocator = wlr_allocator_autocreate(fixture->backend, renderer);
  if (allocator == NULL) {
    wlr_renderer_destroy(renderer);
    close(drm_fd);
    return false;
  }

  fixture->renderer = renderer;
  fixture->allocator = allocator;
  fixture->drm_fd = drm_fd;
  return true;
}

static bool fixture_init(struct fixture* fixture) {
  *fixture = (struct fixture){.drm_fd = -1};
  fixture->display = wl_display_create();
  if (fixture->display == NULL) {
    return false;
  }
  fixture->backend = wlr_headless_backend_create(wl_display_get_event_loop(fixture->display));
  if (fixture->backend == NULL) {
    return false;
  }

  const char* requested_device = getenv("UMBRIELFX_TEST_DRM_DEVICE");
  if (requested_device != NULL) {
    if (!fixture_try_device(fixture, requested_device))
      return false;
    goto create_output;
  }

  drmDevicePtr devices[64] = {0};
  int devices_len = drmGetDevices2(0, devices, 64);
  for (int i = 0; i < devices_len && fixture->renderer == NULL; i++) {
    if (!(devices[i]->available_nodes & (1 << DRM_NODE_RENDER))) {
      continue;
    }
    fixture_try_device(fixture, devices[i]->nodes[DRM_NODE_RENDER]);
  }
  if (devices_len > 0) {
    drmFreeDevices(devices, devices_len);
  }
  if (fixture->renderer == NULL) {
    return false;
  }

create_output:
  fixture->output = wlr_headless_add_output(fixture->backend, TEST_WIDTH, TEST_HEIGHT);
  if (fixture->output == NULL || !wlr_output_init_render(fixture->output, fixture->allocator, fixture->renderer)) {
    return false;
  }
  fx_renderer_set_allocator(fixture->renderer, fixture->allocator);
  return true;
}

static void fixture_finish(struct fixture* fixture) {
  if (fixture->allocator != NULL) {
    wlr_allocator_destroy(fixture->allocator);
  }
  if (fixture->renderer != NULL) {
    wlr_renderer_destroy(fixture->renderer);
  }
  if (fixture->backend != NULL) {
    wlr_backend_destroy(fixture->backend);
  }
  if (fixture->display != NULL) {
    wl_display_destroy(fixture->display);
  }
  if (fixture->drm_fd >= 0) {
    close(fixture->drm_fd);
  }
}

static const struct wlr_drm_format* get_render_format(struct fixture* fixture, uint32_t format) {
  const struct wlr_drm_format_set* formats = fixture->renderer->impl->get_render_formats(fixture->renderer);
  return wlr_drm_format_set_get(formats, format);
}

static struct wlr_buffer* create_output_buffer(struct fixture* fixture, uint32_t format, int width, int height) {
  const struct wlr_drm_format* drm_format = get_render_format(fixture, format);
  if (drm_format == NULL) {
    return NULL;
  }
  return wlr_allocator_create_buffer(fixture->allocator, width, height, drm_format);
}

static bool
read_buffer(struct fixture* fixture, struct wlr_buffer* buffer, uint32_t format, uint32_t stride, void* data) {
  struct wlr_texture* texture = wlr_texture_from_buffer(fixture->renderer, buffer);
  if (texture == NULL) {
    return false;
  }
  bool ok = wlr_texture_read_pixels(
      texture,
      &(struct wlr_texture_read_pixels_options){
          .data = data,
          .format = format,
          .stride = stride,
      }
  );
  wlr_texture_destroy(texture);
  return ok;
}

#define FIXTURE_TEXT_LIMIT (256 * 1024)

static inline char* read_text_file(const char* directory, const char* name) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s", directory, name);
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    perror(path);
    return NULL;
  }
  char* text = calloc(FIXTURE_TEXT_LIMIT + 1, 1);
  if (text != NULL) {
    size_t length = fread(text, 1, FIXTURE_TEXT_LIMIT, file);
    if (ferror(file) || length == FIXTURE_TEXT_LIMIT) {
      free(text);
      text = NULL;
    }
  }
  fclose(file);
  return text;
}

// With BIRI_SHADER_FRAMES set, dumps a frame as a PPM composited over a checkerboard.
static inline bool write_frame(const char* name, int frame, const uint32_t* pixels, int width, int height) {
  const char* directory = getenv("BIRI_SHADER_FRAMES");
  if (directory == NULL)
    return true;
  char flat[256];
  snprintf(flat, sizeof(flat), "%s", name);
  for (char* p = flat; *p; ++p)
    if (*p == '/')
      *p = '-';
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s-%d.ppm", directory, flat, frame);
  FILE* file = fopen(path, "wb");
  if (file == NULL)
    return false;
  fprintf(file, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint32_t pixel = pixels[y * width + x];
      unsigned alpha = pixel >> 24;
      unsigned background = ((x / 8 + y / 8) % 2) ? 32 : 64;
      for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned value = ((pixel >> shift) & 255) + background * (255 - alpha) / 255;
        fputc(value > 255 ? 255 : value, file);
      }
    }
  }
  bool ok = !ferror(file);
  return fclose(file) == 0 && ok;
}

static inline size_t count_differences(const uint32_t* a, const uint32_t* b, size_t count, int tolerance) {
  size_t different = 0;
  for (size_t i = 0; i < count; i++) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      int delta = (int)((a[i] >> shift) & 255) - (int)((b[i] >> shift) & 255);
      if (abs(delta) > tolerance) {
        different++;
        break;
      }
    }
  }
  return different;
}
