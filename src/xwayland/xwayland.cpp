#include "xwayland/xwayland.h"

#include "config/config.h"
#include "core/log.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "server/server.h"
#include "wlr.h"
#include "xwayland/window.h"

#include <cstdlib>
#include <cstring>

namespace umbriel {

  namespace {
    constexpr Logger kLog("xwayland");

    xcb_atom_t resolveAtom(const char* displayName, const char* name) {
      if (displayName == nullptr) {
        return XCB_ATOM_NONE;
      }
      xcb_connection_t* connection = xcb_connect(displayName, nullptr);
      if (connection == nullptr || xcb_connection_has_error(connection) != 0) {
        if (connection != nullptr) {
          xcb_disconnect(connection);
        }
        return XCB_ATOM_NONE;
      }
      const xcb_intern_atom_cookie_t cookie =
          xcb_intern_atom(connection, 0, static_cast<uint16_t>(std::strlen(name)), name);
      xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection, cookie, nullptr);
      const xcb_atom_t atom = reply != nullptr ? reply->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
      std::free(reply);
      xcb_disconnect(connection);
      return atom;
    }
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
    m_focusClearTimer = wl_event_loop_add_timer(wl_display_get_event_loop(server.display()), onFocusClearTimer, this);
    m_selectionRefreshTimer =
        wl_event_loop_add_timer(wl_display_get_event_loop(server.display()), onSelectionRefreshTimer, this);
    m_keyboardFocusChange.notify = onKeyboardFocusChange;
    wl_signal_add(&server.seat()->wlr()->keyboard_state.events.focus_change, &m_keyboardFocusChange);
    kLog.info("Xwayland listening on DISPLAY={}", m_wlr->display_name);
  }

  Xwayland::~Xwayland() {
    if (m_wlr == nullptr) {
      return;
    }
    wl_list_remove(&m_keyboardFocusChange.link);
    if (m_selectionRefreshTimer != nullptr) {
      wl_event_source_remove(m_selectionRefreshTimer);
    }
    // Windows only detach here; the server deletes views before destroying this.
    m_windows.clear();
    wl_list_remove(&m_serverStart.link);
    wl_list_remove(&m_ready.link);
    wl_list_remove(&m_newSurface.link);
    if (m_focusClearTimer != nullptr) {
      wl_event_source_remove(m_focusClearTimer);
    }
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

  bool Xwayland::ownsSurface(const wlr_surface* surface) const {
    return surface != nullptr
        && surface->resource != nullptr
        && m_wlr != nullptr
        && m_wlr->server != nullptr
        && m_wlr->server->client != nullptr
        && wl_resource_get_client(surface->resource) == m_wlr->server->client;
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

  void Xwayland::scheduleFocusClear() {
    if (m_focusClearTimer != nullptr) {
      // A zero Wayland timer is disarmed. One millisecond puts this on the next event-loop turn, after the current
      // dispatch returns and wl_display_run flushes the native client's keyboard enter.
      wl_event_source_timer_update(m_focusClearTimer, 1);
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

  void Xwayland::onKeyboardFocusChange(wl_listener* listener, void* data) {
    Xwayland* self = wl_container_of(listener, self, m_keyboardFocusChange); // NOLINT(modernize-use-auto)
    self->handleKeyboardFocusChange(data);
  }

  int Xwayland::onSelectionRefreshTimer(void* data) {
    auto* self = static_cast<Xwayland*>(data);
    wlr_seat* seat = self->m_server.seat()->wlr();
    if (seat->keyboard_state.focused_surface == nullptr
        || wlr_xwayland_surface_try_from_wlr_surface(seat->keyboard_state.focused_surface) == nullptr
        || self->m_wlr->xwm == nullptr) {
      return 0;
    }
    // Wine reads targets on notification, including while unfocused when wlroots denies the read. Rebinding the
    // same seat republishes native selections after activation without replacing X11-owned sources.
    wlr_xwayland_set_seat(self->m_wlr, seat);
    return 0;
  }

  int Xwayland::onFocusClearTimer(void* data) {
    auto* self = static_cast<Xwayland*>(data);
    wlr_surface* focused = self->m_server.seat()->wlr()->keyboard_state.focused_surface;
    if (focused != nullptr && wlr_xwayland_surface_try_from_wlr_surface(focused) == nullptr) {
      self->clearFocus();
    }
    return 0;
  }

  void Xwayland::handleReady() {
    m_netActiveWindow = resolveAtom(m_wlr->display_name, "_NET_ACTIVE_WINDOW");
    if (m_netActiveWindow == XCB_ATOM_NONE) {
      kLog.warn("could not resolve _NET_ACTIVE_WINDOW");
    }
    kLog.info("Xwayland ready on DISPLAY={}", m_wlr->display_name);
  }

  void Xwayland::handleNewSurface(wlr_xwayland_surface* xsurface) {
    m_windows.push_back(std::make_unique<XwaylandWindow>(m_server, *this, xsurface));
  }

  void Xwayland::handleKeyboardFocusChange(void* data) {
    auto* event = static_cast<wlr_seat_keyboard_focus_change_event*>(data);
    if (event->new_surface == nullptr
        || wlr_xwayland_surface_try_from_wlr_surface(event->new_surface) == nullptr
        || (event->old_surface != nullptr && wlr_xwayland_surface_try_from_wlr_surface(event->old_surface) != nullptr)
        || m_selectionRefreshTimer == nullptr) {
      return;
    }
    // Unmanaged X11 windows can activate after entering the seat. Wait until this dispatch has completed.
    wl_event_source_timer_update(m_selectionRefreshTimer, 1);
  }

  void Xwayland::clearFocus() {
    xcb_connection_t* connection = wlr_xwayland_get_xwm_connection(m_wlr);
    if (connection == nullptr) {
      return;
    }
    constexpr xcb_window_t kNoWindow = XCB_WINDOW_NONE;
    xcb_set_input_focus(connection, XCB_INPUT_FOCUS_NONE, kNoWindow, XCB_CURRENT_TIME);
    if (m_netActiveWindow != XCB_ATOM_NONE) {
      xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
      if (screens.rem != 0) {
        xcb_change_property(
            connection, XCB_PROP_MODE_REPLACE, screens.data->root, m_netActiveWindow, XCB_ATOM_WINDOW, 32, 1, &kNoWindow
        );
      }
    }
    xcb_flush(connection);
  }

} // namespace umbriel
