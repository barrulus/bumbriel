#include "xwayland/xwayland.h"

#include "config/config.h"
#include "core/log.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "server/server.h"
#include "wlr.h"
#include "xwayland/window.h"

namespace umbriel {

  namespace {
    constexpr Logger kLog("xwayland");
  } // namespace

  void grantConfigureRequest(wlr_xwayland_surface* xsurface, const wlr_xwayland_surface_configure_event* event) {
    const uint16_t mask = event->mask;
    wlr_xwayland_surface_configure(
        xsurface, (mask & XCB_CONFIG_WINDOW_X) != 0 ? event->x : xsurface->x,
        (mask & XCB_CONFIG_WINDOW_Y) != 0 ? event->y : xsurface->y,
        (mask & XCB_CONFIG_WINDOW_WIDTH) != 0 ? event->width : xsurface->width,
        (mask & XCB_CONFIG_WINDOW_HEIGHT) != 0 ? event->height : xsurface->height
    );
  }

  Xwayland::Xwayland(Server& server) : m_server(server), m_nativeResolution(config().general.xwaylandNativeResolution) {
    m_wlr = wlr_xwayland_create(server.display(), server.compositor(), /*lazy=*/true);
    if (m_wlr == nullptr) {
      kLog.error("failed to create the Xwayland server");
      return;
    }
    m_outputs = std::make_unique<XwaylandOutputs>(server.display(), server.outputLayout(), m_nativeResolution);
    // wlroots drops the seat whenever the window manager goes away, which happens each time the lazy Xwayland exits
    // with its last client, so hand it over again before every start. wlroots passes the seat to the window manager
    // when Xwayland is ready; setting it from the ready handler instead makes X requests there that can leave the
    // first client's MapRequest unread in xcb's queue.
    m_serverStart.notify = onServerStart;
    wl_signal_add(&m_wlr->server->events.start, &m_serverStart);
    applyCursor(server.cursor()->xcursorManager());
    m_ready.notify = onReady;
    wl_signal_add(&m_wlr->events.ready, &m_ready);
    m_newSurface.notify = onNewSurface;
    wl_signal_add(&m_wlr->events.new_surface, &m_newSurface);
    kLog.info("Xwayland listening on DISPLAY={}", m_wlr->display_name);
  }

  Xwayland::~Xwayland() {
    if (m_wlr == nullptr) {
      return;
    }
    // Windows only detach here; the server deletes views before destroying this.
    m_windows.clear();
    wl_list_remove(&m_serverStart.link);
    wl_list_remove(&m_ready.link);
    wl_list_remove(&m_newSurface.link);
    wlr_xwayland_destroy(m_wlr);
  }

  const char* Xwayland::displayName() const { return m_wlr != nullptr ? m_wlr->display_name : nullptr; }

  void Xwayland::applyCursor(wlr_xcursor_manager* manager) {
    if (m_wlr == nullptr || manager == nullptr || !wlr_xcursor_manager_load(manager, 1.0F)) {
      return;
    }
    wlr_xcursor* xcursor = wlr_xcursor_manager_get_xcursor(manager, "default", 1.0F);
    if (xcursor == nullptr || xcursor->image_count == 0) {
      return;
    }
    wlr_xcursor_image* image = xcursor->images[0];
    wlr_xwayland_set_cursor(
        m_wlr, wlr_xcursor_image_get_buffer(image), static_cast<int32_t>(image->hotspot_x),
        static_cast<int32_t>(image->hotspot_y)
    );
  }

  void Xwayland::forget(XwaylandWindow* window) {
    std::erase_if(m_windows, [window](const std::unique_ptr<XwaylandWindow>& entry) { return entry.get() == window; });
  }

  double Xwayland::surfaceScale(wlr_surface* surface) const {
    if (!m_nativeResolution || surface == nullptr) {
      return 1.0;
    }
    const wlr_xwayland_surface* xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
    if (xsurface == nullptr) {
      return 1.0;
    }
    const auto found = std::ranges::find(m_windows, xsurface, &XwaylandWindow::xsurface);
    return found != m_windows.end() ? (*found)->scale() : 1.0;
  }

  bool Xwayland::advertiseOutputManager(const wl_client* client, const wl_global* global) const {
    return m_outputs->advertise(client, global, m_wlr->server != nullptr ? m_wlr->server->client : nullptr);
  }

  void Xwayland::handleOutputLayoutChange() {
    if (!m_outputs->update()) {
      return;
    }
    for (const auto& window : m_windows) {
      window->syncGeometry();
    }
  }

  void Xwayland::onServerStart(wl_listener* listener, void* /*data*/) {
    Xwayland* self = wl_container_of(listener, self, m_serverStart); // NOLINT(modernize-use-auto)
    wlr_xwayland_set_seat(self->m_wlr, self->m_server.seat()->wlr());
  }

  void Xwayland::onReady(wl_listener* listener, void* /*data*/) {
    Xwayland* self = wl_container_of(listener, self, m_ready); // NOLINT(modernize-use-auto)
    self->handleReady();
  }

  void Xwayland::onNewSurface(wl_listener* listener, void* data) {
    Xwayland* self = wl_container_of(listener, self, m_newSurface); // NOLINT(modernize-use-auto)
    self->handleNewSurface(static_cast<wlr_xwayland_surface*>(data));
  }

  void Xwayland::handleReady() { kLog.info("Xwayland ready on DISPLAY={}", m_wlr->display_name); }

  void Xwayland::handleNewSurface(wlr_xwayland_surface* xsurface) {
    m_windows.push_back(std::make_unique<XwaylandWindow>(m_server, *this, xsurface));
  }

} // namespace umbriel
