#pragma once

#include <memory>
#include <wayland-server-core.h>

struct wlr_xwayland_surface;

namespace umbriel {

  class Server;
  class View;
  class Xwayland;
  class XwaylandUnmanaged;

  // One X11 window for its whole lifetime. While it has a wl_surface, an override-redirect window or one typed as a
  // menu, tooltip, dropdown, or drag icon is an XwaylandUnmanaged, and any other window is a View; a window whose
  // override-redirect flag changes swaps.
  class XwaylandWindow {
  public:
    XwaylandWindow(Server& server, Xwayland& owner, wlr_xwayland_surface* xsurface);
    // Detaches without touching the view: it only runs at server teardown, after the registry deleted every view.
    ~XwaylandWindow();

    XwaylandWindow(const XwaylandWindow&) = delete;
    XwaylandWindow& operator=(const XwaylandWindow&) = delete;

    // The X screen moved relative to the layout: re-place the wrapper's window.
    void syncGeometry();
    [[nodiscard]] wlr_xwayland_surface* xsurface() const { return m_xsurface; }
    // X pixels per layout unit in the window's surface.
    [[nodiscard]] double scale() const;

  private:
    static void onAssociate(wl_listener* listener, void* data);
    static void onDissociate(wl_listener* listener, void* data);
    static void onDestroy(wl_listener* listener, void* data);
    static void onSetOverrideRedirect(wl_listener* listener, void* data);
    static void onSurfaceUnmap(wl_listener* listener, void* data);
    // Before associate there is no wrapper to decide, so the client's geometry request is granted.
    static void onRequestConfigure(wl_listener* listener, void* data);
    void handleAssociate();
    void handleDissociate();
    void handleDestroy();
    void handleSetOverrideRedirect();
    void handleSurfaceUnmap();
    void createWrapper();
    void destroyWrapper();
    void removeListeners();

    Server& m_server;
    Xwayland& m_owner;
    wlr_xwayland_surface* m_xsurface = nullptr;
    View* m_view = nullptr;
    std::unique_ptr<XwaylandUnmanaged> m_unmanaged;
    // The override-redirect flag changed while mapped: swap the wrapper once the surface unmaps.
    bool m_swapOnUnmap = false;
    wl_listener m_associate{};
    wl_listener m_dissociate{};
    wl_listener m_destroy{};
    wl_listener m_setOverrideRedirect{};
    wl_listener m_surfaceUnmap{};
    wl_listener m_requestConfigure{};
  };

} // namespace umbriel
