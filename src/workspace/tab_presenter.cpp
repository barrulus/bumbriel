#include "workspace/tab_presenter.h"

#include "config/config.h"
#include "layout/layout.h"
#include "scene/tab_bar.h"
#include "scene/tab_bar_geometry.h"
#include "scene/tab_label_cache.h"
#include "view/view.h"
#include "workspace/workspace.h"

#include <algorithm>
#include <string_view>

namespace umbriel {

  namespace {

    void replaceAll(std::string& text, std::string_view token, std::string_view value) {
      for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) {
        text.replace(at, token.size(), value);
      }
    }

    bool contains(const wlr_box& box, double x, double y) {
      return box.width > 0
          && box.height > 0
          && x >= box.x
          && y >= box.y
          && x < static_cast<double>(box.x + box.width)
          && y < static_cast<double>(box.y + box.height);
    }

  } // namespace

  TabPresenter::TabPresenter(Workspace& workspace) : m_workspace(workspace), m_labels(TabLabelCache::shared()) {}

  TabPresenter::~TabPresenter() = default;

  void TabPresenter::sync() {
    Layout& layout = m_workspace.layout();
    TabbedContainers* containers = layout.tabbedContainers();
    const bool anyTabbed = containers != nullptr
        && std::ranges::any_of(layout.columns(), [](const Column& column) { return !column.tabs.empty(); });
    if (!anyTabbed && m_members.empty()) {
      return;
    }
    // The focused window is always its group's shown tab, however it got into the group: a consume, a drop, a move, or
    // a swap puts it there without a focus change.
    if (View* focused = m_workspace.focusedView(); anyTabbed && focused != nullptr && layout.columnOf(focused) >= 0) {
      containers->selectTab(focused);
    }
    const std::vector<View*> previous = std::move(m_members);
    m_members.clear();
    if (anyTabbed) {
      for (const Column& column : layout.columns()) {
        for (const TabGroup& group : column.tabs.groups()) {
          presentGroup(column, group);
        }
      }
    }
    for (View* view : previous) {
      if (std::ranges::find(m_members, view) == m_members.end()) {
        clear(view);
      }
    }
  }

  void TabPresenter::focusChanged(View* view) {
    TabbedContainers* containers = m_workspace.layout().tabbedContainers();
    if (containers == nullptr || view == nullptr || m_workspace.layout().columnOf(view) < 0) {
      return;
    }
    // The boxes do not move, so showing another tab needs no arrange.
    if (containers->selectTab(view)) {
      if (const std::optional<GroupRef> ref = groupOf(view)) {
        presentGroup(*ref->column, *ref->group);
      }
    }
  }

  void TabPresenter::memberChanged(const View* view) {
    const std::optional<GroupRef> ref = groupOf(view);
    if (!ref || !barShown(*ref->group)) {
      return;
    }
    if (TabBar* bar = barOf(ref->column->views[ref->group->active])) {
      bar->setModel(modelFor(*ref->column, *ref->group));
    }
  }

  void TabPresenter::released(View* view) {
    if (std::erase(m_members, view) > 0) {
      clear(view);
    }
  }

  void TabPresenter::releaseAll() {
    const std::vector<View*> members = std::move(m_members);
    m_members.clear();
    for (View* view : members) {
      clear(view);
    }
  }

  void TabPresenter::presentGroup(const Column& column, const TabGroup& group) {
    const bool withBar = barShown(group);
    const TabBarModel model = withBar ? modelFor(column, group) : TabBarModel{};
    for (size_t row = group.first; row < group.end() && row < column.views.size(); ++row) {
      View* view = column.views[row];
      if (view == nullptr) {
        continue;
      }
      if (std::ranges::find(m_members, view) == m_members.end()) {
        m_members.push_back(view);
      }
      const bool hidden = row != group.active;
      const bool revealed = view->tabHidden() && !hidden;
      view->setTabHidden(hidden);
      if (revealed) {
        m_workspace.syncViewPresentation(view);
      }
      // A hidden tab keeps its bar, hidden along with it, for the next time it shows.
      if (hidden) {
        continue;
      }
      if (!withBar) {
        view->setChromeAttachment(nullptr);
        continue;
      }
      TabBar* bar = barOf(view);
      if (bar == nullptr) {
        auto created = std::make_unique<TabBar>(view->chromeParent(), m_labels);
        bar = created.get();
        view->setChromeAttachment(std::move(created));
      }
      bar->setModel(model);
    }
  }

  void TabPresenter::clear(View* view) {
    view->setTabHidden(false);
    if (barOf(view) != nullptr) {
      view->setChromeAttachment(nullptr);
    }
  }

  bool TabPresenter::barShown(const TabGroup& group) const {
    return tabBarShown(m_workspace.layoutConfig(), group.count, group.bar);
  }

  TabBarModel TabPresenter::modelFor(const Column& column, const TabGroup& group) const {
    TabBarModel model{
        .tabs = {}, .active = group.active - group.first, .strip = {}, .gap = m_workspace.layoutConfig().gap
    };
    model.tabs.reserve(group.count);
    for (size_t row = group.first; row < group.end() && row < column.views.size(); ++row) {
      const View* view = column.views[row];
      model.tabs.push_back({
          .title = view != nullptr ? titleFor(view, row - group.first) : std::string{},
          // The tab on show needs no flag: its window can say so itself.
          .urgent = view != nullptr && row != group.active && view->urgent(),
      });
    }
    // Beyond max_tabs a scrolling bar shows a run of tabs that follows the active one from where it was.
    const Config::Appearance::TabBar& bar = config().appearance.tabBar;
    const size_t maxVisible = bar.overflow == TabOverflow::Scroll ? static_cast<size_t>(std::max(0, bar.maxTabs)) : 0;
    const TabBarModel* previous = previousModel(column, group);
    model.strip =
        tabStrip(model.tabs.size(), model.active, maxVisible, previous != nullptr ? previous->strip.first : 0);
    return model;
  }

  const TabBarModel* TabPresenter::previousModel(const Column& column, const TabGroup& group) {
    // The tab on show before this update still carries the bar it drew: hidden flags change only after the model.
    for (size_t row = group.first; row < group.end() && row < column.views.size(); ++row) {
      const View* view = column.views[row];
      if (const TabBar* bar = view != nullptr && !view->tabHidden() ? barOf(view) : nullptr) {
        return &bar->model();
      }
    }
    return nullptr;
  }

  std::string TabPresenter::titleFor(const View* view, size_t index) const {
    const char* title = view->title();
    const char* appId = view->appId();
    const std::string_view titleText = title != nullptr ? title : "";
    const std::string_view appIdText = appId != nullptr ? appId : "";
    std::string label = config().appearance.tabBar.titleFormat;
    replaceAll(label, "{title}", titleText.empty() ? appIdText : titleText);
    replaceAll(label, "{app_id}", appIdText);
    replaceAll(label, "{index}", std::to_string(index + 1));
    return label.empty() ? std::string(appIdText) : label;
  }

  std::optional<TabPresenter::GroupRef> TabPresenter::groupOf(const View* view) const {
    const Layout& layout = m_workspace.layout();
    const int index = view != nullptr ? layout.columnOf(view) : -1;
    if (index < 0 || index >= static_cast<int>(layout.columns().size())) {
      return std::nullopt;
    }
    const Column& column = layout.columns()[static_cast<size_t>(index)];
    const TabGroup* group = tabGroupOf(column, view);
    if (group == nullptr) {
      return std::nullopt;
    }
    return GroupRef{.column = &column, .group = group};
  }

  TabBar* TabPresenter::barOf(const View* view) {
    return view != nullptr ? dynamic_cast<TabBar*>(view->chromeAttachment()) : nullptr;
  }

  std::optional<TabHit> TabPresenter::tabAt(double lx, double ly) const {
    for (const Column& column : m_workspace.layout().columns()) {
      for (const TabGroup& group : column.tabs.groups()) {
        if (group.active >= column.views.size()) {
          continue;
        }
        const TabBar* bar = barOf(column.views[group.active]);
        const wlr_box box = bar != nullptr ? bar->layoutBox() : wlr_box{};
        if (!contains(box, lx, ly)) {
          continue;
        }
        const TabBarPoint point = bar->pointAt(lx - box.x, ly - box.y);
        switch (point.part) {
        case TabBarPart::None:
          return std::nullopt;
        case TabBarPart::Tab:
          return TabHit{.part = point.part, .tab = column.views[group.first + point.index], .index = point.index};
        case TabBarPart::PreviousTab:
        case TabBarPart::NextTab:
          // A button belongs to the bar, so it names the tab on show as the group it cycles.
          return TabHit{.part = point.part, .tab = column.views[group.active], .index = group.active - group.first};
        }
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<TabDrop> TabPresenter::dropSlot(int columnIndex, double lx, double ly) const {
    const Layout& layout = m_workspace.layout();
    if (columnIndex < 0 || columnIndex >= static_cast<int>(layout.columns().size())) {
      return std::nullopt;
    }
    const Column& column = layout.columns()[static_cast<size_t>(columnIndex)];
    const bool afterActive = m_workspace.layoutConfig().tabs.newTabPosition == NewTabPosition::AfterActive;
    // Rows of a column stack across x when the strip scrolls vertically, and across y otherwise.
    const bool stackAlongX = m_workspace.scrollingVertical();
    for (const TabGroup& group : column.tabs.groups()) {
      if (group.active >= column.views.size()) {
        continue;
      }
      const TabBar* bar = barOf(column.views[group.active]);
      const wlr_box box = bar != nullptr ? bar->layoutBox() : wlr_box{};
      // Over the bar, or close enough to it to mean it, the drop lands between the tabs under the pointer. Close enough
      // is half the bar's thickness again on either side of it.
      const bool across = bar != nullptr && tabBarAcross(bar->style().position);
      const wlr_box near = across
          ? wlr_box{.x = box.x, .y = box.y - box.height / 2, .width = box.width, .height = box.height * 2}
          : wlr_box{.x = box.x - box.width / 2, .y = box.y, .width = box.width * 2, .height = box.height};
      if (contains(near, lx, ly)) {
        // Measured along the bar, so a point beside it counts as the nearest place on it.
        const TabBarDrop drop = bar->dropAt(across ? lx - box.x : 0.0, across ? 0.0 : ly - box.y);
        return TabDrop{
            .row = static_cast<int>(group.first + std::min(drop.row, group.count)),
            .member = column.views[group.active],
            .hint = {
                .x = box.x + drop.marker.x,
                .y = box.y + drop.marker.y,
                .width = drop.marker.width,
                .height = drop.marker.height,
            },
        };
      }
      // Over the middle half of the group's tabs the drop goes where new tabs go, and the whole bar lights up. The
      // quarters at either end are left to the drop beside the group.
      const wlr_box tabs = layout.targetBox(column.views[group.active]);
      const int start = stackAlongX ? tabs.x : tabs.y;
      const int extent = stackAlongX ? tabs.width : tabs.height;
      const double along = stackAlongX ? lx : ly;
      const bool middle = along >= start + (extent / 4.0) && along < start + (extent * 3.0 / 4.0);
      if (!contains(tabs, lx, ly) || !middle) {
        continue;
      }
      const size_t row = afterActive ? group.active + 1 : group.end();
      return TabDrop{
          .row = static_cast<int>(row), .member = column.views[group.active], .hint = box.width > 0 ? box : tabs
      };
    }
    return std::nullopt;
  }

  View* TabPresenter::stepTarget(int direction) const { return stepFrom(m_workspace.focusedView(), direction); }

  View* TabPresenter::stepFrom(const View* member, int direction) const {
    return step(member, direction, m_workspace.layoutConfig().tabs.wrapFocus);
  }

  View* TabPresenter::cycleFrom(const View* member, int direction) const { return step(member, direction, true); }

  View* TabPresenter::step(const View* member, int direction, bool wrap) const {
    const std::optional<GroupRef> ref = groupOf(member);
    if (!ref || ref->group->count < 2 || direction == 0) {
      return nullptr;
    }
    const TabGroup& group = *ref->group;
    const auto count = static_cast<int>(group.count);
    int target = static_cast<int>(group.active - group.first) + (direction > 0 ? 1 : -1);
    if (target < 0 || target >= count) {
      if (!wrap) {
        return nullptr;
      }
      target = (target + count) % count;
    }
    return ref->column->views[group.first + static_cast<size_t>(target)];
  }

  View* TabPresenter::indexTarget(int index) const {
    const std::optional<GroupRef> ref = groupOf(m_workspace.focusedView());
    if (!ref || index == 0) {
      return nullptr;
    }
    const auto count = static_cast<int>(ref->group->count);
    const int target = index > 0 ? index - 1 : count + index;
    if (target < 0 || target >= count) {
      return nullptr;
    }
    return ref->column->views[ref->group->first + static_cast<size_t>(target)];
  }

} // namespace umbriel
