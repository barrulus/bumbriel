// Capture one isolated toplevel through the public protocol and print its centre RGB.
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

struct State {
  wl_shm* shm = nullptr;
  ext_foreign_toplevel_list_v1* list = nullptr;
  ext_foreign_toplevel_image_capture_source_manager_v1* sourceManager = nullptr;
  ext_image_copy_capture_manager_v1* copyManager = nullptr;
  ext_foreign_toplevel_handle_v1* target = nullptr;
  std::string title;
  uint32_t width = 0, height = 0;
  bool constraints = false, ready = false, failed = false, argb = false, xrgb = false;
};
static const ext_foreign_toplevel_handle_v1_listener handleListener = {
    .closed = [](void*, ext_foreign_toplevel_handle_v1*) {},
    .done = [](void*, ext_foreign_toplevel_handle_v1*) {},
    .title =
        [](void* data, ext_foreign_toplevel_handle_v1* handle, const char* title) {
          auto& state = *static_cast<State*>(data);
          if (state.title == title)
            state.target = handle;
        },
    .app_id = [](void*, ext_foreign_toplevel_handle_v1*, const char*) {},
    .identifier = [](void*, ext_foreign_toplevel_handle_v1*, const char*) {},
};
static const ext_foreign_toplevel_list_v1_listener listListener = {
    .toplevel = [](
                    void* data, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* handle
                ) { ext_foreign_toplevel_handle_v1_add_listener(handle, &handleListener, data); },
    .finished = [](void*, ext_foreign_toplevel_list_v1*) {},
};
static const wl_registry_listener registryListener = {
    .global =
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
          auto& state = *static_cast<State*>(data);
#define BIND(member, type)                                                                                             \
  if (strcmp(interface, type##_interface.name) == 0)                                                                   \
  state.member = static_cast<type*>(wl_registry_bind(registry, name, &type##_interface, 1))
          BIND(shm, wl_shm);
          BIND(list, ext_foreign_toplevel_list_v1);
          BIND(sourceManager, ext_foreign_toplevel_image_capture_source_manager_v1);
          BIND(copyManager, ext_image_copy_capture_manager_v1);
#undef BIND
          if (state.list != nullptr && strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0)
            ext_foreign_toplevel_list_v1_add_listener(state.list, &listListener, data);
        },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
};
static const ext_image_copy_capture_session_v1_listener sessionListener = {
    .buffer_size =
        [](void* data, ext_image_copy_capture_session_v1*, uint32_t w, uint32_t h) {
          auto& state = *static_cast<State*>(data);
          state.width = w;
          state.height = h;
        },
    .shm_format =
        [](void* data, ext_image_copy_capture_session_v1*, uint32_t f) {
          auto& state = *static_cast<State*>(data);
          state.argb |= f == WL_SHM_FORMAT_ARGB8888;
          state.xrgb |= f == WL_SHM_FORMAT_XRGB8888;
        },
    .dmabuf_device = [](void*, ext_image_copy_capture_session_v1*, wl_array*) {},
    .dmabuf_format = [](void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {},
    .done = [](void* data, ext_image_copy_capture_session_v1*) { static_cast<State*>(data)->constraints = true; },
    .stopped = [](void* data, ext_image_copy_capture_session_v1*) { static_cast<State*>(data)->failed = true; },
};
static const ext_image_copy_capture_frame_v1_listener frameListener = {
    .transform = [](void*, ext_image_copy_capture_frame_v1*, uint32_t) {},
    .damage = [](void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {},
    .presentation_time = [](void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {},
    .ready = [](void* data, ext_image_copy_capture_frame_v1*) { static_cast<State*>(data)->ready = true; },
    .failed =
        [](void* data, ext_image_copy_capture_frame_v1*, uint32_t reason) {
          std::println(stderr, "capture failed: {}", reason);
          static_cast<State*>(data)->failed = true;
        },
};
int main(int argc, char** argv) {
  if (argc != 2)
    return 2;
  State state;
  state.title = argv[1];
  auto* display = wl_display_connect(nullptr);
  if (display == nullptr)
    return 2;
  auto* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registryListener, &state);
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);
  if (state.target == nullptr || state.shm == nullptr || state.sourceManager == nullptr || state.copyManager == nullptr)
    return 2;
  auto* source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(state.sourceManager, state.target);
  auto* session = ext_image_copy_capture_manager_v1_create_session(state.copyManager, source, 0);
  ext_image_copy_capture_session_v1_add_listener(session, &sessionListener, &state);
  while (!state.constraints && !state.failed)
    if (wl_display_dispatch(display) < 0)
      return 2;
  if (state.failed || (!state.argb && !state.xrgb) || state.width == 0 || state.height == 0)
    return 2;
  size_t bytes = state.width * state.height * 4;
  int fd = memfd_create("capture-test", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, static_cast<off_t>(bytes)) < 0)
    return 2;
  auto* pixels = static_cast<uint32_t*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (pixels == MAP_FAILED)
    return 2;
  auto* pool = wl_shm_create_pool(state.shm, fd, static_cast<int32_t>(bytes));
  auto* buffer = wl_shm_pool_create_buffer(
      pool, 0, state.width, state.height, state.width * 4, state.argb ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888
  );
  auto* frame = ext_image_copy_capture_session_v1_create_frame(session);
  ext_image_copy_capture_frame_v1_add_listener(frame, &frameListener, &state);
  ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
  ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, state.width, state.height);
  ext_image_copy_capture_frame_v1_capture(frame);
  while (!state.ready && !state.failed)
    if (wl_display_dispatch(display) < 0)
      return 2;
  uint32_t pixel = pixels[(state.height / 2) * state.width + state.width / 2];
  if (!state.failed)
    std::println("{} {} {}", (pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255);
  wl_display_disconnect(display);
  munmap(pixels, bytes);
  close(fd);
  return state.failed ? 1 : 0;
}
