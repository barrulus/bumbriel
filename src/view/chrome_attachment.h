#pragma once

#include <memory>

struct wlr_scene_tree;

namespace umbriel {

  // Where a view's content sits inside its frame, and what decides how chrome around it is drawn.
  struct ViewChromeGeometry {
    int contentX = 0;
    int contentY = 0;
    int contentWidth = 0;
    int contentHeight = 0;
    // Border thickness drawn around the content, 0 when undecorated.
    int borderInset = 0;
    // Scale of the output presenting the view, for chrome that renders text.
    float scale = 1.0F;
    // Set while the view draws nothing around its content: unmapped, fullscreen, or maximized to the edges.
    bool suppressed = false;
    // How much smaller than the view chrome is drawn, for a scaled preview of it such as an overview card. Every other
    // field stays in the view's own logical units.
    double zoom = 1.0;
    bool operator==(const ViewChromeGeometry&) const = default;
  };

  // Compositor chrome a view carries beside its own borders, such as a tab bar. Its scene nodes hang under the view's
  // frame, so it follows every position, slide, and visibility change the view makes. The view owns it, destroys it
  // before the frame, and reports to it every change chrome follows; it knows nothing else about it.
  class ViewChromeAttachment {
  public:
    ViewChromeAttachment() = default;
    virtual ~ViewChromeAttachment() = default;
    ViewChromeAttachment(const ViewChromeAttachment&) = delete;
    ViewChromeAttachment& operator=(const ViewChromeAttachment&) = delete;

    virtual void layout(const ViewChromeGeometry& geometry) = 0;
    virtual void setFocused(bool focused) = 0;
    // The view's fade, drag, and overview opacity, which chrome fades with.
    virtual void setAlpha(float alpha) = 0;
    // The configuration was reloaded.
    virtual void reloadConfig() = 0;

    // A copy of this chrome under `parent`, for a scaled preview of the view, or null for chrome with nothing to show
    // there. The preview is laid out like any chrome and follows this one through syncPreview.
    [[nodiscard]] virtual std::unique_ptr<ViewChromeAttachment> makePreview(wlr_scene_tree* /*parent*/) const {
      return nullptr;
    }
    // Bring `preview`, made by this chrome's makePreview, in line with what this chrome shows.
    virtual void syncPreview(ViewChromeAttachment& /*preview*/) const {}
  };

} // namespace umbriel
