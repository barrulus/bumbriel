// Maps a native Wayland toplevel and installs the exact scale-1 default-theme
// image as a client-owned cursor surface. It proves cursor promotion is gated
// by Xwayland ownership rather than pixel content alone.

#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <print>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
extern "C" {
#include <wlr/xcursor.h>
}

namespace {
  constexpr int kDefaultWidth = 640;
  constexpr int kDefaultHeight = 480;
  constexpr int kCursorSize = 24;

  struct Buffer {
    wl_buffer* resource = nullptr;
    void* pixels = MAP_FAILED;
    size_t size = 0;
  };

  struct State {
    wl_display* display = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    xdg_wm_base* wmBase = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* xdgSurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    wl_surface* cursorSurface = nullptr;
    Buffer windowBuffer;
    Buffer cursorBuffer;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    int cursorWidth = 0;
    int cursorHeight = 0;
    int cursorHotspotX = 0;
    int cursorHotspotY = 0;
    bool resizePending = true;
    bool mapped = false;
    bool failed = false;
  };

  void destroyBuffer(Buffer& buffer) {
    if (buffer.resource != nullptr) {
      wl_buffer_destroy(buffer.resource);
    }
    if (buffer.pixels != MAP_FAILED) {
      munmap(buffer.pixels, buffer.size);
    }
    buffer = {};
  }

  Buffer createBuffer(State& state, int width, int height, const char* label) {
    Buffer buffer;
    const int stride = width * 4;
    buffer.size = static_cast<size_t>(stride) * static_cast<size_t>(height);
    const int fd = memfd_create(label, MFD_CLOEXEC);
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
    buffer.resource = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
  }

  bool createWindowBuffer(State& state) {
    Buffer buffer = createBuffer(state, state.width, state.height, "umbriel-wayland-cursor-window");
    if (buffer.resource == nullptr) {
      destroyBuffer(buffer);
      return false;
    }
    std::fill_n(static_cast<uint32_t*>(buffer.pixels), buffer.size / sizeof(uint32_t), 0xFF000000U);
    destroyBuffer(state.windowBuffer);
    state.windowBuffer = buffer;
    return true;
  }

  bool createCursorBuffer(State& state) {
    wlr_xcursor_theme* theme = wlr_xcursor_theme_load(std::getenv("XCURSOR_THEME"), kCursorSize);
    if (theme == nullptr) {
      return false;
    }
    wlr_xcursor* cursor = wlr_xcursor_theme_get_cursor(theme, "default");
    if (cursor == nullptr || cursor->image_count == 0 || cursor->images[0] == nullptr) {
      wlr_xcursor_theme_destroy(theme);
      return false;
    }
    const wlr_xcursor_image& image = *cursor->images[0];
    if (image.width == 0
        || image.height == 0
        || image.width > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || image.height > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || image.hotspot_x >= image.width
        || image.hotspot_y >= image.height) {
      wlr_xcursor_theme_destroy(theme);
      return false;
    }

    state.cursorWidth = static_cast<int>(image.width);
    state.cursorHeight = static_cast<int>(image.height);
    state.cursorHotspotX = static_cast<int>(image.hotspot_x);
    state.cursorHotspotY = static_cast<int>(image.hotspot_y);
    state.cursorBuffer = createBuffer(state, state.cursorWidth, state.cursorHeight, "umbriel-wayland-cursor-image");
    if (state.cursorBuffer.resource == nullptr) {
      destroyBuffer(state.cursorBuffer);
      wlr_xcursor_theme_destroy(theme);
      return false;
    }
    std::memcpy(state.cursorBuffer.pixels, image.buffer, state.cursorBuffer.size);
    wlr_xcursor_theme_destroy(theme);
    return true;
  }

  void pointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t, wl_fixed_t) {
    auto& state = *static_cast<State*>(data);
    if (surface != state.surface) {
      return;
    }
    wl_pointer_set_cursor(pointer, serial, state.cursorSurface, state.cursorHotspotX, state.cursorHotspotY);
    wl_surface_attach(state.cursorSurface, state.cursorBuffer.resource, 0, 0);
    wl_surface_damage_buffer(state.cursorSurface, 0, 0, state.cursorWidth, state.cursorHeight);
    wl_surface_commit(state.cursorSurface);
    std::println("cursor theme-surface");
  }

  void pointerLeave(void*, wl_pointer*, uint32_t, wl_surface*) {}
  void pointerMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) {}
  void pointerButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) {}
  void pointerAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
  void pointerFrame(void*, wl_pointer*) {}
  void pointerAxisSource(void*, wl_pointer*, uint32_t) {}
  void pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
  void pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
  void pointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
  void pointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}
#ifdef WL_POINTER_WARP_SINCE_VERSION
  void pointerWarp(void*, wl_pointer*, wl_fixed_t, wl_fixed_t) {}
#endif

  constexpr wl_pointer_listener kPointerListener = {
      .enter = pointerEnter,
      .leave = pointerLeave,
      .motion = pointerMotion,
      .button = pointerButton,
      .axis = pointerAxis,
      .frame = pointerFrame,
      .axis_source = pointerAxisSource,
      .axis_stop = pointerAxisStop,
      .axis_discrete = pointerAxisDiscrete,
      .axis_value120 = pointerAxisValue120,
      .axis_relative_direction = pointerAxisRelativeDirection,
#ifdef WL_POINTER_WARP_SINCE_VERSION
      .warp = pointerWarp,
#endif
  };

  void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    auto& state = *static_cast<State*>(data);
    const bool hasPointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (hasPointer && state.pointer == nullptr) {
      state.pointer = wl_seat_get_pointer(seat);
      wl_pointer_add_listener(state.pointer, &kPointerListener, &state);
    } else if (!hasPointer && state.pointer != nullptr) {
      wl_pointer_release(state.pointer);
      state.pointer = nullptr;
    }
  }

  void seatName(void*, wl_seat*, const char*) {}
  constexpr wl_seat_listener kSeatListener = {
      .capabilities = seatCapabilities,
      .name = seatName,
  };

  void wmBasePing(void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); }
  constexpr xdg_wm_base_listener kWmBaseListener = {.ping = wmBasePing};

  void xdgConfigure(void* data, xdg_surface* surface, uint32_t serial) {
    auto& state = *static_cast<State*>(data);
    xdg_surface_ack_configure(surface, serial);
    if (state.resizePending && !createWindowBuffer(state)) {
      state.failed = true;
      return;
    }
    state.resizePending = false;
    wl_surface_attach(state.surface, state.windowBuffer.resource, 0, 0);
    wl_surface_damage_buffer(state.surface, 0, 0, state.width, state.height);
    wl_surface_commit(state.surface);
    if (!state.mapped) {
      state.mapped = true;
      std::println("mapped {}x{}", state.width, state.height);
    }
  }
  constexpr xdg_surface_listener kXdgSurfaceListener = {.configure = xdgConfigure};

  void toplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*) {
    auto& state = *static_cast<State*>(data);
    if (width <= 0 || height <= 0 || (width == state.width && height == state.height)) {
      return;
    }
    state.width = width;
    state.height = height;
    state.resizePending = true;
  }
  void toplevelClose(void* data, xdg_toplevel*) { static_cast<State*>(data)->failed = true; }
  void toplevelConfigureBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
  void toplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}
  constexpr xdg_toplevel_listener kToplevelListener = {
      .configure = toplevelConfigure,
      .close = toplevelClose,
      .configure_bounds = toplevelConfigureBounds,
      .wm_capabilities = toplevelWmCapabilities,
  };

  void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto& state = *static_cast<State*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
      state.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
      state.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
      state.seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7U)));
      wl_seat_add_listener(state.seat, &kSeatListener, &state);
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
      state.wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
      xdg_wm_base_add_listener(state.wmBase, &kWmBaseListener, &state);
    }
  }
  void registryRemove(void*, wl_registry*, uint32_t) {}
  constexpr wl_registry_listener kRegistryListener = {.global = registryGlobal, .global_remove = registryRemove};
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(stderr, "usage: wayland-cursor-client TITLE");
    return EXIT_FAILURE;
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);

  State state;
  state.display = wl_display_connect(nullptr);
  if (state.display == nullptr) {
    std::println(stderr, "wayland-cursor-client: failed to connect to WAYLAND_DISPLAY");
    return EXIT_FAILURE;
  }
  wl_registry* registry = wl_display_get_registry(state.display);
  wl_registry_add_listener(registry, &kRegistryListener, &state);
  wl_display_roundtrip(state.display);
  if (state.compositor == nullptr || state.shm == nullptr || state.seat == nullptr || state.wmBase == nullptr) {
    std::println(stderr, "wayland-cursor-client: missing required Wayland globals");
    return EXIT_FAILURE;
  }
  if (!createCursorBuffer(state)) {
    std::println(stderr, "wayland-cursor-client: failed to create the cursor buffer");
    return EXIT_FAILURE;
  }

  state.cursorSurface = wl_compositor_create_surface(state.compositor);
  state.surface = wl_compositor_create_surface(state.compositor);
  state.xdgSurface = xdg_wm_base_get_xdg_surface(state.wmBase, state.surface);
  xdg_surface_add_listener(state.xdgSurface, &kXdgSurfaceListener, &state);
  state.toplevel = xdg_surface_get_toplevel(state.xdgSurface);
  xdg_toplevel_add_listener(state.toplevel, &kToplevelListener, &state);
  xdg_toplevel_set_title(state.toplevel, argv[1]);
  wl_surface_commit(state.surface);

  while (!state.failed && wl_display_dispatch(state.display) >= 0) {
  }

  destroyBuffer(state.cursorBuffer);
  destroyBuffer(state.windowBuffer);
  wl_display_disconnect(state.display);
  return state.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
