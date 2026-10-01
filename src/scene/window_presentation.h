#pragma once

#include "core/animation.h"
#include "scene/presentation.h"

#include <memory>
#include <nlohmann/json_fwd.hpp>

struct timespec;

namespace umbriel {
  class Output;
  class Server;
  class View;

  // One output-local authored transaction borrowing a native open/close clock.
  // Native owners keep geometry, lifecycle deadlines and client ownership.
  class WindowPresentation {
  public:
    WindowPresentation(Server& server, Output& output);
    ~WindowPresentation();
    void opening(View& view);
    void closing(View& view, CloseSnapshotId snapshot);
    void topologyChanged();
    void prepareFrame(bool animate);
    void frameSubmitted(bool success);
    void sendFrameDone(const timespec& when);
    void cancel(PresentationFallback reason);
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool renderLocked() const;
    [[nodiscard]] nlohmann::json status() const;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace umbriel
