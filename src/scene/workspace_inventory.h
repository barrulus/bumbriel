#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace umbriel {
  class WorkspaceGroup;

  // Internal runtime ownership of a complete, stable workspace inventory.
  // Acquiring this hold never changes selection or renders a presentation.
  // Explicit native mutation invalidates it before changing any identities;
  // ordinary dynamic pruning waits until the last owning handle is released.
  class WorkspaceInventoryHold {
  public:
    ~WorkspaceInventoryHold();
    WorkspaceInventoryHold(const WorkspaceInventoryHold&) = delete;
    WorkspaceInventoryHold& operator=(const WorkspaceInventoryHold&) = delete;
    bool commitSelection(std::string_view identity);
    bool activateSelection(std::string_view identity);
    [[nodiscard]] bool valid() const { return m_state->group != nullptr; }
    [[nodiscard]] const std::vector<std::string>& identities() const { return m_state->identities; }
    [[nodiscard]] const std::string& original() const { return m_state->original; }

  private:
    friend class WorkspaceGroup;
    struct State {
      WorkspaceGroup* group = nullptr;
      std::vector<std::string> identities;
      std::string original;
      std::function<void()> invalidated;
    };
    explicit WorkspaceInventoryHold(std::shared_ptr<State> state) : m_state(std::move(state)) {}
    std::shared_ptr<State> m_state;
  };
} // namespace umbriel
