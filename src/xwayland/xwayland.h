#pragma once

#include "xwayland/outputs.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <wayland-server-core.h>

struct wl_client;
struct wl_global;
struct wlr_surface;
struct wlr_xcursor_manager;
struct wlr_xwayland;
struct wlr_xwayland_surface;
struct wlr_xwayland_surface_configure_event;

namespace umbriel {

  class Server;
  class XwaylandWindow;

  // Configure the window as the client asked; fields the request leaves out keep their current value.
  void grantConfigureRequest(wlr_xwayland_surface* xsurface, const wlr_xwayland_surface_configure_event* event);

  // The wlroots Xwayland server. It starts lazily: the X display socket exists from construction, and the Xwayland
  // process spawns when the first X client connects. Every X11 window it announces gets an XwaylandWindow.
  class Xwayland {
  public:
    explicit Xwayland(Server& server);
    ~Xwayland();

    Xwayland(const Xwayland&) = delete;
    Xwayland& operator=(const Xwayland&) = delete;

    // False when wlroots could not create the server; the caller drops the object.
    [[nodiscard]] bool available() const { return m_wlr != nullptr; }
    // The value children get as DISPLAY.
    [[nodiscard]] const char* displayName() const;
    // The cursor X clients see over their windows until they set their own. The buffer belongs to `manager`, so this
    // must run again before a replaced manager is destroyed.
    void applyCursor(wlr_xcursor_manager* manager);
    // Called by a window whose X11 surface was destroyed; deletes it.
    void forget(XwaylandWindow* window);
    // Mapping between layout and X coordinates.
    [[nodiscard]] const XwaylandOutputs& outputs() const { return *m_outputs; }
    // X pixels per layout unit in `surface`: the scale its X11 window is drawn at. 1 for every other surface.
    [[nodiscard]] double surfaceScale(wlr_surface* surface) const;
    // Whether an xdg-output manager global may be advertised to `client`.
    [[nodiscard]] bool advertiseOutputManager(const wl_client* client, const wl_global* global) const;
    // Republishes the X screen and moves every X11 window to match it.
    void handleOutputLayoutChange();
    // An Xwayland surface just yielded the keyboard to a native Wayland client. Clear both X focus channels after the
    // Wayland enter has reached the client, matching the cross-process handoff used by xwayland-satellite.
    void scheduleFocusClear();

  private:
    static void onServerStart(wl_listener* listener, void* data);
    static void onReady(wl_listener* listener, void* data);
    static void onNewSurface(wl_listener* listener, void* data);
    static int onFocusClearTimer(void* data);
    void handleReady();
    void handleNewSurface(wlr_xwayland_surface* xsurface);
    void clearFocus();

    Server& m_server;
    wlr_xwayland* m_wlr = nullptr;
    bool m_nativeResolution = false;
    std::unique_ptr<XwaylandOutputs> m_outputs;
    std::vector<std::unique_ptr<XwaylandWindow>> m_windows;
    uint32_t m_netActiveWindow = 0;
    wl_event_source* m_focusClearTimer = nullptr;
    wl_listener m_serverStart{};
    wl_listener m_ready{};
    wl_listener m_newSurface{};
  };

} // namespace umbriel
