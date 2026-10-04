#include "scene/tab_bar_style.h"

namespace umbriel {

  TabBarStyle resolveTabBarStyle(const Config& config) {
    const Config::Appearance::TabBar& bar = config.appearance.tabBar;
    return {
        .look = bar.style,
        .position = bar.position,
        .height = bar.height,
        .font = bar.font,
        .padding = bar.padding,
        .tabGap = bar.tabGap,
        .cornerRadius = bar.cornerRadius >= 0 ? bar.cornerRadius : config.appearance.cornerRadius,
        .titleAlign = bar.titleAlign,
        .colors = config.colors.tabBar,
    };
  }

} // namespace umbriel
