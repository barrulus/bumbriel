// Captures one frame of the toplevel whose ext-foreign-toplevel-list-v1 title matches, through
// ext-image-capture-source-v1 and ext-image-copy-capture-v1, and prints the mean "r g b" (0-255) of its 8x8 centre.

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <poll.h>
#include <print>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

namespace {
  struct Toplevel {
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    std::string title;
    bool done = false;
    bool closed = false;
  };

  struct Buffer {
    wl_buffer* resource = nullptr;
    void* pixels = MAP_FAILED;
    size_t size = 0;
    int width = 0;
    int height = 0;
  };

  struct State {
    wl_shm* shm = nullptr;
    ext_foreign_toplevel_list_v1* toplevelList = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1* sourceManager = nullptr;
    ext_image_copy_capture_manager_v1* copyManager = nullptr;
    std::vector<std::unique_ptr<Toplevel>> toplevels;
    bool listFinished = false;

    // Session buffer constraints.
    uint32_t bufferWidth = 0;
    uint32_t bufferHeight = 0;
    bool haveShmFormat = false;
    uint32_t shmFormat = WL_SHM_FORMAT_ARGB8888;
    bool constraintsDone = false;
    bool sessionStopped = false;

    // Frame result.
    bool ready = false;
    bool failed = false;
    uint32_t failureReason = 0;
  };

  void toplevelTitle(void* data, ext_foreign_toplevel_handle_v1*, const char* title) {
    static_cast<Toplevel*>(data)->title = title != nullptr ? title : "";
  }
  void toplevelAppId(void*, ext_foreign_toplevel_handle_v1*, const char*) {}
  void toplevelIdentifier(void*, ext_foreign_toplevel_handle_v1*, const char*) {}
  void toplevelDone(void* data, ext_foreign_toplevel_handle_v1*) { static_cast<Toplevel*>(data)->done = true; }
  void toplevelClosed(void* data, ext_foreign_toplevel_handle_v1*) { static_cast<Toplevel*>(data)->closed = true; }

  constexpr ext_foreign_toplevel_handle_v1_listener kToplevelListener{
      .closed = toplevelClosed,
      .done = toplevelDone,
      .title = toplevelTitle,
      .app_id = toplevelAppId,
      .identifier = toplevelIdentifier,
  };

  void listToplevel(void* data, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* handle) {
    auto& state = *static_cast<State*>(data);
    auto toplevel = std::make_unique<Toplevel>();
    toplevel->handle = handle;
    ext_foreign_toplevel_handle_v1_add_listener(handle, &kToplevelListener, toplevel.get());
    state.toplevels.push_back(std::move(toplevel));
  }
  void listFinished(void* data, ext_foreign_toplevel_list_v1*) { static_cast<State*>(data)->listFinished = true; }

  constexpr ext_foreign_toplevel_list_v1_listener kListListener{
      .toplevel = listToplevel,
      .finished = listFinished,
  };

  void sessionBufferSize(void* data, ext_image_copy_capture_session_v1*, uint32_t width, uint32_t height) {
    auto& state = *static_cast<State*>(data);
    state.bufferWidth = width;
    state.bufferHeight = height;
  }
  void sessionShmFormat(void* data, ext_image_copy_capture_session_v1*, uint32_t format) {
    auto& state = *static_cast<State*>(data);
    if (!state.haveShmFormat) {
      state.haveShmFormat = true;
      state.shmFormat = format;
    }
  }
  void sessionDmabufDevice(void*, ext_image_copy_capture_session_v1*, wl_array*) {}
  void sessionDmabufFormat(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {}
  void sessionDone(void* data, ext_image_copy_capture_session_v1*) {
    static_cast<State*>(data)->constraintsDone = true;
  }
  void sessionStopped(void* data, ext_image_copy_capture_session_v1*) {
    static_cast<State*>(data)->sessionStopped = true;
  }

  constexpr ext_image_copy_capture_session_v1_listener kSessionListener{
      .buffer_size = sessionBufferSize,
      .shm_format = sessionShmFormat,
      .dmabuf_device = sessionDmabufDevice,
      .dmabuf_format = sessionDmabufFormat,
      .done = sessionDone,
      .stopped = sessionStopped,
  };

  void frameTransform(void*, ext_image_copy_capture_frame_v1*, uint32_t) {}
  void frameDamage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {}
  void framePresentationTime(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {}
  void frameReady(void* data, ext_image_copy_capture_frame_v1*) { static_cast<State*>(data)->ready = true; }
  void frameFailed(void* data, ext_image_copy_capture_frame_v1*, uint32_t reason) {
    auto& state = *static_cast<State*>(data);
    state.failed = true;
    state.failureReason = reason;
  }

  constexpr ext_image_copy_capture_frame_v1_listener kFrameListener{
      .transform = frameTransform,
      .damage = frameDamage,
      .presentation_time = framePresentationTime,
      .ready = frameReady,
      .failed = frameFailed,
  };

  void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto& state = *static_cast<State*>(data);
    if (std::strcmp(interface, wl_shm_interface.name) == 0) {
      state.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
      state.toplevelList = static_cast<ext_foreign_toplevel_list_v1*>(
          wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, std::min(version, 1U))
      );
      ext_foreign_toplevel_list_v1_add_listener(state.toplevelList, &kListListener, &state);
    } else if (std::strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
      state.sourceManager = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(wl_registry_bind(
          registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, std::min(version, 1U)
      ));
    } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
      state.copyManager = static_cast<ext_image_copy_capture_manager_v1*>(
          wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, std::min(version, 1U))
      );
    }
  }
  void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

  constexpr wl_registry_listener kRegistryListener{
      .global = registryGlobal,
      .global_remove = registryGlobalRemove,
  };

  // Blocks until `done` reports true, the display errors, or the wall-clock deadline passes.
  bool pumpUntil(wl_display* display, const std::chrono::steady_clock::time_point& deadline, auto done) {
    const int displayFd = wl_display_get_fd(display);
    while (!done()) {
      if (wl_display_flush(display) < 0) {
        return false;
      }
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0) {
        return false;
      }
      pollfd fds[1] = {{.fd = displayFd, .events = POLLIN, .revents = 0}};
      const int rc = poll(fds, 1, static_cast<int>(remaining.count()));
      if (rc < 0) {
        return false;
      }
      if (rc == 0) {
        return false;
      }
      if ((fds[0].revents & POLLIN) != 0 && wl_display_dispatch(display) < 0) {
        return false;
      }
    }
    return true;
  }

  // Allocates an ARGB8888/XRGB8888 wl_shm buffer of the session's advertised size.
  Buffer createBuffer(State& state) {
    Buffer buffer{.width = static_cast<int>(state.bufferWidth), .height = static_cast<int>(state.bufferHeight)};
    const int stride = buffer.width * 4;
    buffer.size = static_cast<size_t>(stride) * static_cast<size_t>(buffer.height);
    const int fd = memfd_create("umbriel-toplevel-capture-client", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(buffer.size)) < 0) {
      if (fd >= 0) {
        close(fd);
      }
      return buffer;
    }
    buffer.pixels = mmap(nullptr, buffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer.pixels == MAP_FAILED) {
      close(fd);
      return buffer;
    }
    wl_shm_pool* pool = wl_shm_create_pool(state.shm, fd, static_cast<int>(buffer.size));
    buffer.resource = wl_shm_pool_create_buffer(pool, 0, buffer.width, buffer.height, stride, state.shmFormat);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
  }
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(stderr, "usage: toplevel-capture-client TITLE");
    return 1;
  }
  const std::string_view wantedTitle = argv[1];
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

  wl_display* display = wl_display_connect(nullptr);
  if (display == nullptr) {
    std::println(stderr, "toplevel-capture-client: cannot connect to WAYLAND_DISPLAY");
    return 1;
  }

  State state;
  wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &kRegistryListener, &state);
  if (wl_display_roundtrip(display) < 0) {
    std::println(stderr, "toplevel-capture-client: registry roundtrip failed");
    return 1;
  }
  if (state.shm == nullptr
      || state.toplevelList == nullptr
      || state.sourceManager == nullptr
      || state.copyManager == nullptr) {
    std::println(stderr, "toplevel-capture-client: a required global was not advertised");
    return 1;
  }

  // The first roundtrip delivers the toplevel events; further ones deliver each handle's title/done.
  for (int roundtrip = 0; roundtrip < 4; ++roundtrip) {
    if (wl_display_roundtrip(display) < 0) {
      std::println(stderr, "toplevel-capture-client: toplevel enumeration failed");
      return 1;
    }
    if (std::ranges::any_of(state.toplevels, [](const auto& toplevel) { return toplevel->done; })) {
      break;
    }
  }

  const auto target = std::ranges::find_if(state.toplevels, [wantedTitle](const auto& toplevel) {
    return !toplevel->closed && toplevel->done && toplevel->title == wantedTitle;
  });
  if (target == state.toplevels.end()) {
    std::println(stderr, "toplevel-capture-client: no toplevel titled '{}'", wantedTitle);
    return 1;
  }

  ext_image_capture_source_v1* source =
      ext_foreign_toplevel_image_capture_source_manager_v1_create_source(state.sourceManager, (*target)->handle);
  // 0: an empty options bitfield, so no cursor is painted onto the captured frame.
  ext_image_copy_capture_session_v1* session =
      ext_image_copy_capture_manager_v1_create_session(state.copyManager, source, 0);
  ext_image_copy_capture_session_v1_add_listener(session, &kSessionListener, &state);

  if (!pumpUntil(display, deadline, [&] { return state.constraintsDone || state.sessionStopped; })) {
    std::println(stderr, "toplevel-capture-client: timed out waiting for buffer constraints");
    return 1;
  }
  if (state.sessionStopped || state.bufferWidth == 0 || state.bufferHeight == 0 || !state.haveShmFormat) {
    std::println(stderr, "toplevel-capture-client: session offered no usable shm buffer");
    return 1;
  }

  Buffer buffer = createBuffer(state);
  if (buffer.resource == nullptr || buffer.pixels == MAP_FAILED) {
    std::println(stderr, "toplevel-capture-client: failed to allocate the capture buffer");
    return 1;
  }

  ext_image_copy_capture_frame_v1* frame = ext_image_copy_capture_session_v1_create_frame(session);
  ext_image_copy_capture_frame_v1_add_listener(frame, &kFrameListener, &state);
  ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer.resource);
  ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, INT32_MAX, INT32_MAX);
  ext_image_copy_capture_frame_v1_capture(frame);

  if (!pumpUntil(display, deadline, [&] { return state.ready || state.failed; })) {
    std::println(stderr, "toplevel-capture-client: timed out waiting for the captured frame");
    return 1;
  }
  if (state.failed) {
    std::println(stderr, "toplevel-capture-client: capture failed (reason {})", state.failureReason);
    return 1;
  }

  const auto* pixels = static_cast<const uint8_t*>(buffer.pixels);
  const int stride = buffer.width * 4;
  const int startX = std::max(0, buffer.width / 2 - 4);
  const int startY = std::max(0, buffer.height / 2 - 4);
  const int endX = std::min(buffer.width, startX + 8);
  const int endY = std::min(buffer.height, startY + 8);
  uint64_t sumR = 0;
  uint64_t sumG = 0;
  uint64_t sumB = 0;
  uint64_t count = 0;
  for (int y = startY; y < endY; ++y) {
    for (int x = startX; x < endX; ++x) {
      const uint8_t* pixel = pixels + static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x) * 4;
      // wl_shm ARGB8888/XRGB8888 store channels as B, G, R, A/X in memory on a little-endian host.
      sumB += pixel[0];
      sumG += pixel[1];
      sumR += pixel[2];
      ++count;
    }
  }
  if (count == 0) {
    std::println(stderr, "toplevel-capture-client: captured buffer too small to sample");
    return 1;
  }
  std::println("{} {} {}", sumR / count, sumG / count, sumB / count);
  return 0;
}
