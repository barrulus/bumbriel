#pragma once

#include <wayland-server-core.h>

struct wlr_compositor;

namespace umbriel {

  // wlroots signals a linux-drm-syncobj release point only when the client commits its next buffer, so the last
  // buffer before a NULL attach stays pending while the surface is unmapped. A client that waits for that release
  // before it draws again (Chromium re-showing a window after a tab drag) never draws again. This signals the point
  // once the compositor has dropped the unmapped buffer.
  class SyncobjUnmapRelease {
  public:
    explicit SyncobjUnmapRelease(wlr_compositor* compositor);
    ~SyncobjUnmapRelease();

    SyncobjUnmapRelease(const SyncobjUnmapRelease&) = delete;
    SyncobjUnmapRelease& operator=(const SyncobjUnmapRelease&) = delete;

  private:
    static void onNewSurface(wl_listener* listener, void* data);

    wl_listener m_newSurface{};
  };

} // namespace umbriel
