#pragma once

#include "core/animation.h"
#include "scene/presentation.h"

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string_view>

struct timespec;
struct wlr_pointer;

namespace umbriel {
  class Server;
  class Output;
  enum class NavigationSource : uint8_t;

  // An output-local authored workspace set. Native workspace membership and
  // focus change only after the selected landing frame has been submitted.
  class WorkspacePresentation final : public Animatable {
  public:
    WorkspacePresentation(Server& server, Output& output);
    ~WorkspacePresentation() override;
    bool begin(std::string_view preset);
    bool select(std::string_view identity);
    bool step(int direction);
    bool beginNavigation(wlr_pointer* pointer, NavigationSource source);
    void updateNavigation(double dx, double dy, uint32_t timeMsec);
    void endNavigation(bool cancelled, uint32_t timeMsec);
    void handleTouchpadAxis(wlr_pointer* pointer, bool vertical, double delta, uint32_t timeMsec);
    void handleTouchpadFrame();
    // A primary press/touch is consumed even if the authored frame cannot be picked.
    bool activateAt(double layoutX, double layoutY);
    bool accept();
    bool dismiss();
    void cancel(PresentationFallback reason);
    void prepareFrame(bool animate);
    void frameSubmitted(bool success);
    void sendFrameDone(const timespec& when);
    [[nodiscard]] bool active() const;
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
