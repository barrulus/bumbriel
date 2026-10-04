#include "xwayland/window.h"

#include "server/server.h"
#include "view/view.h"
#include "wlr.h"
#include "xwayland/unmanaged.h"
#include "xwayland/xwayland.h"

#include <algorithm>
#include <array>

namespace umbriel {

  namespace {

    // The window types of menus, tooltips, dropdowns, and drag icons. A client may leave such a window to the window
    // manager instead of making it override-redirect, yet it still places the window itself, next to what opened it.
    constexpr std::array kPopupWindowTypes{
        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_MENU,       WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_POPUP_MENU, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLTIP,
        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_COMBO,      WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DND,
    };

    bool isPopup(const wlr_xwayland_surface* xsurface) {
      return xsurface->override_redirect || std::ranges::any_of(kPopupWindowTypes, [xsurface](auto type) {
               return wlr_xwayland_surface_has_window_type(xsurface, type);
             });
    }

  } // namespace

  XwaylandWindow::XwaylandWindow(Server& server, Xwayland& owner, wlr_xwayland_surface* xsurface)
      : m_server(server), m_owner(owner), m_xsurface(xsurface) {
    // No wrapper until associate: the wl_surface does not exist before it.
    m_associate.notify = onAssociate;
    wl_signal_add(&xsurface->events.associate, &m_associate);
    m_dissociate.notify = onDissociate;
    wl_signal_add(&xsurface->events.dissociate, &m_dissociate);
    m_destroy.notify = onDestroy;
    wl_signal_add(&xsurface->events.destroy, &m_destroy);
    m_setOverrideRedirect.notify = onSetOverrideRedirect;
    wl_signal_add(&xsurface->events.set_override_redirect, &m_setOverrideRedirect);
    m_requestConfigure.notify = onRequestConfigure;
    wl_signal_add(&xsurface->events.request_configure, &m_requestConfigure);
    m_surfaceUnmap.notify = onSurfaceUnmap;
  }

  XwaylandWindow::~XwaylandWindow() {
    removeListeners();
    m_unmanaged.reset();
  }

  void XwaylandWindow::removeListeners() {
    for (wl_listener* listener :
         {&m_associate, &m_dissociate, &m_destroy, &m_setOverrideRedirect, &m_requestConfigure, &m_surfaceUnmap}) {
      if (listener->link.next != nullptr) {
        wl_list_remove(&listener->link);
        listener->link = {};
      }
    }
  }

  void XwaylandWindow::onAssociate(wl_listener* listener, void* /*data*/) {
    XwaylandWindow* self = wl_container_of(listener, self, m_associate); // NOLINT(modernize-use-auto)
    self->handleAssociate();
  }

  void XwaylandWindow::onDissociate(wl_listener* listener, void* /*data*/) {
    XwaylandWindow* self = wl_container_of(listener, self, m_dissociate); // NOLINT(modernize-use-auto)
    self->handleDissociate();
  }

  void XwaylandWindow::onDestroy(wl_listener* listener, void* /*data*/) {
    XwaylandWindow* self = wl_container_of(listener, self, m_destroy); // NOLINT(modernize-use-auto)
    self->handleDestroy();
  }

  void XwaylandWindow::onSetOverrideRedirect(wl_listener* listener, void* /*data*/) {
    XwaylandWindow* self = wl_container_of(listener, self, m_setOverrideRedirect); // NOLINT(modernize-use-auto)
    self->handleSetOverrideRedirect();
  }

  void XwaylandWindow::onSurfaceUnmap(wl_listener* listener, void* /*data*/) {
    XwaylandWindow* self = wl_container_of(listener, self, m_surfaceUnmap); // NOLINT(modernize-use-auto)
    self->handleSurfaceUnmap();
  }

  void XwaylandWindow::onRequestConfigure(wl_listener* listener, void* data) {
    XwaylandWindow* self = wl_container_of(listener, self, m_requestConfigure); // NOLINT(modernize-use-auto)
    // A view or unmanaged window answers for itself once it exists.
    if (self->m_view == nullptr && self->m_unmanaged == nullptr) {
      grantConfigureRequest(self->m_xsurface, static_cast<const wlr_xwayland_surface_configure_event*>(data));
    }
  }

  void XwaylandWindow::handleAssociate() {
    createWrapper();
    // Added after the wrapper's own unmap listener, so a view snapshots its close animation before a swap.
    wl_signal_add(&m_xsurface->surface->events.unmap, &m_surfaceUnmap);
  }

  void XwaylandWindow::handleDissociate() {
    if (m_surfaceUnmap.link.next != nullptr) {
      wl_list_remove(&m_surfaceUnmap.link);
      m_surfaceUnmap.link = {};
    }
    m_swapOnUnmap = false;
    destroyWrapper();
  }

  void XwaylandWindow::handleDestroy() {
    removeListeners();
    destroyWrapper();
    m_owner.forget(this); // deletes this
  }

  void XwaylandWindow::handleSetOverrideRedirect() {
    if (m_view == nullptr && m_unmanaged == nullptr) {
      return; // associate reads the flag
    }
    if (m_xsurface->surface != nullptr && m_xsurface->surface->mapped) {
      m_swapOnUnmap = true;
      return;
    }
    destroyWrapper();
    createWrapper();
  }

  void XwaylandWindow::handleSurfaceUnmap() {
    if (!m_swapOnUnmap) {
      return;
    }
    m_swapOnUnmap = false;
    destroyWrapper();
    createWrapper();
  }

  void XwaylandWindow::createWrapper() {
    if (isPopup(m_xsurface)) {
      m_unmanaged = std::make_unique<XwaylandUnmanaged>(m_server, m_xsurface);
    } else {
      m_view = &m_server.adoptView(std::make_unique<View>(m_server, m_xsurface));
    }
  }

  void XwaylandWindow::syncGeometry() {
    if (m_view != nullptr) {
      m_view->resyncXwaylandGeometry();
    }
    if (m_unmanaged != nullptr) {
      m_unmanaged->syncPosition();
    }
  }

  double XwaylandWindow::scale() const {
    if (m_view != nullptr) {
      return m_view->xwaylandScale();
    }
    return m_unmanaged != nullptr ? m_unmanaged->scale() : 1.0;
  }

  void XwaylandWindow::destroyWrapper() {
    m_unmanaged.reset();
    if (View* view = m_view) {
      m_view = nullptr;
      view->roleDestroyed();
    }
  }

} // namespace umbriel
