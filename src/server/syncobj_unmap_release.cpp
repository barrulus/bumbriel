#include "server/syncobj_unmap_release.h"

#include "wlr.h"

#include <cstdint>

namespace umbriel {

  namespace {

    struct SurfaceWatch {
      wlr_surface* surface = nullptr;
      // Set while the client's last buffer is unmapped and its release point is still pending.
      wlr_buffer* buffer = nullptr;
      wlr_drm_syncobj_timeline* timeline = nullptr;
      uint64_t point = 0;
      wl_listener clientCommit{};
      wl_listener destroy{};
      wl_listener bufferRelease{};
      wl_listener bufferDestroy{};

      void disarm() {
        if (buffer == nullptr) {
          return;
        }
        wl_list_remove(&bufferRelease.link);
        wl_list_remove(&bufferDestroy.link);
        wlr_drm_syncobj_timeline_unref(timeline);
        buffer = nullptr;
        timeline = nullptr;
      }

      void signal() {
        wlr_drm_syncobj_timeline_signal(timeline, point);
        disarm();
      }
    };

    void onBufferRelease(wl_listener* listener, void* /*data*/) {
      SurfaceWatch* watch;
      watch = wl_container_of(listener, watch, bufferRelease);
      watch->signal();
    }

    void onBufferDestroy(wl_listener* listener, void* /*data*/) {
      SurfaceWatch* watch;
      watch = wl_container_of(listener, watch, bufferDestroy);
      watch->signal();
    }

    void onClientCommit(wl_listener* listener, void* /*data*/) {
      SurfaceWatch* watch;
      watch = wl_container_of(listener, watch, clientCommit);
      wlr_surface* surface = watch->surface;
      if ((surface->pending.committed & WLR_SURFACE_STATE_BUFFER) == 0) {
        return;
      }
      // A new buffer replaces the synced state, and wlroots then signals the previous release point itself.
      watch->disarm();
      if (surface->pending.buffer != nullptr || surface->buffer == nullptr) {
        return;
      }
      uint64_t point = 0;
      wlr_drm_syncobj_timeline* timeline = umbrielfx_linux_drm_syncobj_release_point(surface, &point);
      if (timeline == nullptr) {
        return;
      }
      watch->buffer = &surface->buffer->base;
      watch->timeline = wlr_drm_syncobj_timeline_ref(timeline);
      watch->point = point;
      watch->bufferRelease.notify = onBufferRelease;
      wl_signal_add(&watch->buffer->events.release, &watch->bufferRelease);
      watch->bufferDestroy.notify = onBufferDestroy;
      wl_signal_add(&watch->buffer->events.destroy, &watch->bufferDestroy);
    }

    void onSurfaceDestroy(wl_listener* listener, void* /*data*/) {
      SurfaceWatch* watch;
      watch = wl_container_of(listener, watch, destroy);
      // Destroying the surface finishes the synced state, which signals the pending point.
      watch->disarm();
      wl_list_remove(&watch->clientCommit.link);
      wl_list_remove(&watch->destroy.link);
      delete watch;
    }

  } // namespace

  SyncobjUnmapRelease::SyncobjUnmapRelease(wlr_compositor* compositor) {
    m_newSurface.notify = onNewSurface;
    wl_signal_add(&compositor->events.new_surface, &m_newSurface);
  }

  SyncobjUnmapRelease::~SyncobjUnmapRelease() { wl_list_remove(&m_newSurface.link); }

  void SyncobjUnmapRelease::onNewSurface(wl_listener* /*listener*/, void* data) {
    auto* watch = new SurfaceWatch();
    watch->surface = static_cast<wlr_surface*>(data);
    watch->clientCommit.notify = onClientCommit;
    wl_signal_add(&watch->surface->events.client_commit, &watch->clientCommit);
    watch->destroy.notify = onSurfaceDestroy;
    wl_signal_add(&watch->surface->events.destroy, &watch->destroy);
  }

} // namespace umbriel
