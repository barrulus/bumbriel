// Captures one toplevel through ext-image-copy-capture and prints its centre pixel as "r g b".
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

namespace {

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

  void handleClosed(void*, ext_foreign_toplevel_handle_v1*) {}
  void handleDone(void*, ext_foreign_toplevel_handle_v1*) {}
  void handleTitle(void* data, ext_foreign_toplevel_handle_v1* handle, const char* title) {
    auto* state = static_cast<State*>(data);
    if (state->title == title) {
      state->target = handle;
    }
  }
  void handleAppId(void*, ext_foreign_toplevel_handle_v1*, const char*) {}
  void handleIdentifier(void*, ext_foreign_toplevel_handle_v1*, const char*) {}

  constexpr ext_foreign_toplevel_handle_v1_listener kHandleListener{
      .closed = handleClosed,
      .done = handleDone,
      .title = handleTitle,
      .app_id = handleAppId,
      .identifier = handleIdentifier,
  };

  void handleToplevel(void* data, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* handle) {
    ext_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, data);
  }
  void handleFinished(void*, ext_foreign_toplevel_list_v1*) {}

  constexpr ext_foreign_toplevel_list_v1_listener kListListener{
      .toplevel = handleToplevel,
      .finished = handleFinished,
  };

  void handleGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
    auto* state = static_cast<State*>(data);
    if (std::strcmp(interface, wl_shm_interface.name) == 0) {
      state->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
      state->list = static_cast<ext_foreign_toplevel_list_v1*>(
          wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1)
      );
      ext_foreign_toplevel_list_v1_add_listener(state->list, &kListListener, data);
    } else if (std::strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
      state->sourceManager = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(
          wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1)
      );
    } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
      state->copyManager = static_cast<ext_image_copy_capture_manager_v1*>(
          wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1)
      );
    }
  }
  void handleGlobalRemove(void*, wl_registry*, uint32_t) {}

  constexpr wl_registry_listener kRegistryListener{
      .global = handleGlobal,
      .global_remove = handleGlobalRemove,
  };

  void handleBufferSize(void* data, ext_image_copy_capture_session_v1*, uint32_t width, uint32_t height) {
    auto* state = static_cast<State*>(data);
    state->width = width;
    state->height = height;
  }
  void handleShmFormat(void* data, ext_image_copy_capture_session_v1*, uint32_t format) {
    auto* state = static_cast<State*>(data);
    state->argb |= format == WL_SHM_FORMAT_ARGB8888;
    state->xrgb |= format == WL_SHM_FORMAT_XRGB8888;
  }
  void handleDmabufDevice(void*, ext_image_copy_capture_session_v1*, wl_array*) {}
  void handleDmabufFormat(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {}
  void handleSessionDone(void* data, ext_image_copy_capture_session_v1*) {
    static_cast<State*>(data)->constraints = true;
  }
  void handleSessionStopped(void* data, ext_image_copy_capture_session_v1*) {
    static_cast<State*>(data)->failed = true;
  }

  constexpr ext_image_copy_capture_session_v1_listener kSessionListener{
      .buffer_size = handleBufferSize,
      .shm_format = handleShmFormat,
      .dmabuf_device = handleDmabufDevice,
      .dmabuf_format = handleDmabufFormat,
      .done = handleSessionDone,
      .stopped = handleSessionStopped,
  };

  void handleTransform(void*, ext_image_copy_capture_frame_v1*, uint32_t) {}
  void handleDamage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {}
  void handlePresentationTime(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {}
  void handleFrameReady(void* data, ext_image_copy_capture_frame_v1*) { static_cast<State*>(data)->ready = true; }
  void handleFrameFailed(void* data, ext_image_copy_capture_frame_v1*, uint32_t reason) {
    std::println(stderr, "capture failed: {}", reason);
    static_cast<State*>(data)->failed = true;
  }

  constexpr ext_image_copy_capture_frame_v1_listener kFrameListener{
      .transform = handleTransform,
      .damage = handleDamage,
      .presentation_time = handlePresentationTime,
      .ready = handleFrameReady,
      .failed = handleFrameFailed,
  };

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return 2;
  }
  State state;
  state.title = argv[1];
  auto* display = wl_display_connect(nullptr);
  if (display == nullptr) {
    return 2;
  }
  auto* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &kRegistryListener, &state);
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);
  if (state.target == nullptr
      || state.shm == nullptr
      || state.sourceManager == nullptr
      || state.copyManager == nullptr) {
    return 2;
  }
  auto* source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(state.sourceManager, state.target);
  auto* session = ext_image_copy_capture_manager_v1_create_session(state.copyManager, source, 0);
  ext_image_copy_capture_session_v1_add_listener(session, &kSessionListener, &state);
  while (!state.constraints && !state.failed) {
    if (wl_display_dispatch(display) < 0) {
      return 2;
    }
  }
  if (state.failed || (!state.argb && !state.xrgb) || state.width == 0 || state.height == 0) {
    return 2;
  }
  const size_t bytes = state.width * state.height * 4;
  const int fd = memfd_create("capture-test", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, static_cast<off_t>(bytes)) < 0) {
    return 2;
  }
  auto* pixels = static_cast<uint32_t*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (pixels == MAP_FAILED) {
    return 2;
  }
  auto* pool = wl_shm_create_pool(state.shm, fd, static_cast<int32_t>(bytes));
  auto* buffer = wl_shm_pool_create_buffer(
      pool, 0, state.width, state.height, state.width * 4, state.argb ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888
  );
  auto* frame = ext_image_copy_capture_session_v1_create_frame(session);
  ext_image_copy_capture_frame_v1_add_listener(frame, &kFrameListener, &state);
  ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
  ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, state.width, state.height);
  ext_image_copy_capture_frame_v1_capture(frame);
  while (!state.ready && !state.failed) {
    if (wl_display_dispatch(display) < 0) {
      return 2;
    }
  }
  const uint32_t pixel = pixels[(state.height / 2) * state.width + state.width / 2];
  if (!state.failed) {
    std::println("{} {} {}", (pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255);
  }
  wl_display_disconnect(display);
  munmap(pixels, bytes);
  close(fd);
  return state.failed ? 1 : 0;
}
