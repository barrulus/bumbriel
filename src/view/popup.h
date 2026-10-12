#pragma once
#include "scene/surface_blur.h"

#include <wayland-server-core.h>

struct wlr_xdg_popup;
struct wlr_scene_tree;

namespace umbriel {

  class Popup {
  public:
    explicit Popup(wlr_xdg_popup* popup, wlr_scene_tree* parentTree = nullptr, wlr_scene_tree* captureTree = nullptr);
    ~Popup();

    Popup(const Popup&) = delete;
    Popup& operator=(const Popup&) = delete;

  private:
    static void onCommit(wl_listener* listener, void* data);
    static void onReposition(wl_listener* listener, void* data);
    static void onMap(wl_listener* listener, void* data);
    static void onUnmap(wl_listener* listener, void* data);
    static void onDestroy(wl_listener* listener, void* data);

    void handleCommit();
    void unconstrain();
    void handleMap();
    void handleDestroy();

    wlr_xdg_popup* m_popup = nullptr;
    SurfaceBlur m_blur;

    wl_listener m_commit{};
    wl_listener m_reposition{};
    wl_listener m_map{};
    wl_listener m_unmap{};
    wl_listener m_destroy{};
  };

} // namespace umbriel
