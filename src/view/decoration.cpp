#include "view/decoration.h"

#include "config/config.h"
#include "config/shaders.h"
#include "scene/border_rect.h"
#include "scene/color.h"
#include "scene/decoration_shader.h"
#include "scene/effects.h"

extern "C" {
#include <umbrielfx/render/animation.h>
#include <umbrielfx/render/decoration.h>
}

// clang-format off
#include "wlr.h"
// clang-format on

namespace umbriel {

  // Borders
  void ViewDecoration::ensureBorders(wlr_scene_tree* parent) {
    if (m_borderTree != nullptr) {
      return;
    }
    m_borderTree = wlr_scene_tree_create(parent);
    float innerColor[4];
    float outerColor[4];
    premultiplied(innerColor, config().colors.border.unfocused, 1.0F);
    premultiplied(outerColor, config().colors.border.outer, 1.0F);
    m_border = wlr_scene_border_create(m_borderTree, innerColor, outerColor);
    // The punched hole protects client content, so keep the border above the
    // toplevel surface that would otherwise cover its outermost pixels.
    wlr_scene_node_raise_to_top(&m_borderTree->node);
    updateShader();
  }

  void ViewDecoration::setShaderFocused(bool focused) {
    m_shaderFocused = focused;
    updateShader();
  }

  void ViewDecoration::updateShader() {
    if (m_border == nullptr)
      return;
    auto* shader = (m_shaderFocused || !m_shaderConfig.focusedOnly) ? decorationShader(m_shaderConfig) : nullptr;
    const auto parameters =
        decorationParameters(m_shaderConfig, static_cast<float>(m_shaderConfig.padding), 1.0F, m_lightSuppressed);
    wlr_scene_border_set_shader(m_border, shader, &parameters);
    const auto program = shader ? preparedEffect(m_shaderConfig.effect, EffectScope::BorderOuter) : nullptr;
    wlr_scene_border_set_postprocess(m_border, program ? program->postprocess.get() : nullptr);
    if (shader != nullptr && m_shaderConfig.palette) {
      const auto palette = shaderPalette(config().colors);
      wlr_scene_border_set_palette(m_border, palette.data(), kShaderPaletteCount);
    } else {
      wlr_scene_border_set_palette(m_border, nullptr, 0);
    }
    const int padding = shader != nullptr ? m_shaderConfig.padding : 0;
    if (padding != m_shaderPadding) {
      m_shaderPadding = padding;
      updateBorderGeometry(m_border->clipped_region.area.width, m_border->clipped_region.area.height);
    }
  }

  void ViewDecoration::setLightSuppressed(bool suppressed) {
    if (m_lightSuppressed == suppressed)
      return;
    m_lightSuppressed = suppressed;
    updateShader();
  }

  bool ViewDecoration::bordersVisible() const { return m_borderTree != nullptr && m_borderTree->node.enabled; }

  void ViewDecoration::setBordersEnabled(bool enabled) {
    if (m_borderTree != nullptr) {
      wlr_scene_node_set_enabled(&m_borderTree->node, enabled);
    }
  }

  void ViewDecoration::updateBorderGeometry(int contentWidth, int contentHeight) {
    if (m_border == nullptr) {
      return;
    }

    const auto& appearance = config().appearance;
    const BorderRing ring = padBorderRing(
        makeBorderRing(
            contentWidth, contentHeight, appearance.cornerRadius, appearance.borderWidth, appearance.outerBorderWidth
        ),
        m_shaderPadding
    );
    applyBorderGeometry(m_border, ring, appearance.borderWidth, appearance.outerBorderWidth);
  }

  void ViewDecoration::setBorderColor(bool focused, bool scratchpad, float alpha) {
    if (m_borderTree == nullptr) {
      return;
    }
    const auto& baseColor = scratchpad
        ? (focused ? config().colors.border.scratchpadFocused : config().colors.border.scratchpadUnfocused)
        : (focused ? config().colors.border.focused : config().colors.border.unfocused);
    setBorderRawColor(baseColor, alpha);
  }

  void ViewDecoration::setBorderRawColor(const std::array<float, 4>& baseColor, float alpha) {
    if (m_border == nullptr) {
      return;
    }
    float innerColor[4];
    float outerColor[4];
    premultiplied(innerColor, baseColor, alpha);
    premultiplied(outerColor, config().colors.border.outer, alpha);
    wlr_scene_border_set_colors(m_border, innerColor, outerColor);
  }

  bool ViewDecoration::borderGeometryStale(int contentWidth, int contentHeight) const {
    if (m_border == nullptr) {
      return false;
    }
    const auto& appearance = config().appearance;
    const BorderRing ring = padBorderRing(
        makeBorderRing(
            contentWidth, contentHeight, appearance.cornerRadius, appearance.borderWidth, appearance.outerBorderWidth
        ),
        m_shaderPadding
    );
    return m_border->width != ring.box.width || m_border->height != ring.box.height;
  }

  void ViewDecoration::snapshotBorders(
      wlr_scene_tree* snapshot, const std::array<float, 4>& innerColor, float opacity, std::vector<BorderSnapshot>& out
  ) const {
    if (!bordersVisible() || m_border == nullptr) {
      return;
    }

    wlr_scene_border* copy = wlr_scene_border_create(snapshot, m_border->inner_color, m_border->outer_color);
    if (copy == nullptr) {
      return;
    }
    wlr_scene_border_set_geometry(
        copy, m_border->width, m_border->height, m_border->inner_width, m_border->outer_width, m_border->clipped_region,
        m_border->seam_corners, m_border->outer_corners
    );
    wlr_scene_node_set_position(
        &copy->node, m_borderTree->node.x + m_border->node.x, m_borderTree->node.y + m_border->node.y
    );
    wlr_scene_node_copy_animations_for_snapshot(&copy->node, &m_borderTree->node);
    wlr_scene_border_copy_shader(copy, m_border);
    // Straight colours at the opacity the ring is drawn with right now, so the fade starts from what is on screen
    // and stays in step with the content buffers, which keep their current opacity as their base.
    BorderSnapshot captured{.node = copy, .innerColor = innerColor, .outerColor = config().colors.border.outer};
    captured.innerColor[3] *= opacity;
    captured.outerColor[3] *= opacity;
    out.push_back(captured);
  }

  // Blur
  void ViewDecoration::applyRule(const ResolvedWindowRule& rule, const DecorationShaderConfig& shader) {
    m_shaderConfig = shader;
    updateShader();
    m_blurOptions = SurfaceBlurOptions{
        .ignoreAlpha = static_cast<float>(rule.blurIgnoreAlpha.value_or(0.0)),
        .enabled = rule.blur.value_or(false),
        .optimized = rule.blurOptimized,
    };
    m_popupBlurOptions = SurfaceBlurOptions{
        .ignoreAlpha = static_cast<float>(rule.blurIgnoreAlpha.value_or(0.0)),
        .enabled = rule.blurPopups.value_or(false),
        .optimized = rule.blurOptimized,
    };
  }

  void ViewDecoration::updateBlur(
      wlr_scene_tree* tree, wlr_surface* surface, const wlr_box& nodeBox, const wlr_box& geometry, int radius,
      const wlr_box* clip, float surfaceOpacity, float blurAlpha
  ) {
    m_blur.setAlpha(blurAlpha);
    m_blur.update(tree, surface, nodeBox, geometry, radius, clip, m_blurOptions, surfaceOpacity);
  }

  void ViewDecoration::hideBlur() { m_blur.hide(); }

  // Shadow
  void ViewDecoration::createShadow(wlr_scene_tree* frame) {
    m_shadowContainer = wlr_scene_tree_create(frame);
    wlr_scene_node_lower_to_bottom(&m_shadowContainer->node);
  }

  void ViewDecoration::poolShadow(wlr_scene_tree* frame, wlr_scene_tree* pool, int x, int y, bool enabled) {
    if (m_shadowContainer == nullptr) {
      return;
    }
    if (pool == nullptr) {
      if (!m_shadowPooled) {
        return;
      }
      wlr_scene_node_reparent(&m_shadowContainer->node, frame);
      wlr_scene_node_lower_to_bottom(&m_shadowContainer->node);
      wlr_scene_node_set_position(&m_shadowContainer->node, 0, 0);
      wlr_scene_node_set_enabled(&m_shadowContainer->node, true);
      m_shadowPooled = false;
      return;
    }
    wlr_scene_node_reparent(&m_shadowContainer->node, pool);
    wlr_scene_node_set_position(&m_shadowContainer->node, x, y);
    wlr_scene_node_set_enabled(&m_shadowContainer->node, enabled);
    m_shadowPooled = true;
  }

  void ViewDecoration::setShadowPosition(int x, int y) {
    if (m_shadowPooled) {
      wlr_scene_node_set_position(&m_shadowContainer->node, x, y);
    }
  }

  void ViewDecoration::setShadowEnabled(bool enabled) {
    if (m_shadowPooled) {
      wlr_scene_node_set_enabled(&m_shadowContainer->node, enabled);
    }
  }

  void ViewDecoration::updateShadow(int contentWidth, int contentHeight, int borderInset, int cornerRadius) {
    if (m_shadowContainer == nullptr) {
      return;
    }
    m_shadow.update(m_shadowContainer, contentWidth, contentHeight, borderInset, cornerRadius);
  }

  void ViewDecoration::hideShadow() { m_shadow.hide(); }

  void ViewDecoration::setAlpha(float decorationAlpha, float blurAlpha) {
    m_shadow.setAlpha(decorationAlpha);
    m_blur.setAlpha(blurAlpha);
  }

  void ViewDecoration::hideEffects() {
    m_blur.hide();
    m_shadow.hide();
  }

} // namespace umbriel
