#pragma once
#include "core/animation.h"
#include "scene/presentation.h"

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string_view>
struct timespec;
namespace umbriel {
  class Server;
  class Output;
  class WorkspaceTransition final : public Animatable {
  public:
    WorkspaceTransition(Server& server, Output& output);
    ~WorkspaceTransition() override;
    bool begin(std::string_view destination, bool interactive = false);
    bool gestureProgress(double progress);
    bool retargetInteractive(std::string_view destination, double progress);
    bool settle(bool commit, double velocity = 0);
    void cancel(PresentationFallback reason);
    void prepareFrame(bool animate);
    void frameSubmitted(bool success);
    void sendFrameDone(const timespec& when);
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool interactive() const;
    [[nodiscard]] nlohmann::json status() const;
    [[nodiscard]] AnimationPhase animationPhase() const override { return AnimationPhase::Overlays; }
    bool tickAnimations(uint64_t nowMsec) override;
    [[nodiscard]] bool hasActiveAnimations() const override;
    [[nodiscard]] bool animatesOn(const Output* output) const override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace umbriel
