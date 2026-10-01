#include "scene/presentation.h"

#include <algorithm>
#include <cmath>

namespace umbriel {

  bool PresentationLease::acquire(PresentationRequest request, const PresentationAdmission& admission) {
    auto decline = [this](PresentationFallback reason) {
      m_fallback = reason;
      return false;
    };
    if (admission.locked) {
      cancel(PresentationFallback::Locked);
      return decline(PresentationFallback::Locked);
    }
    if (active() || admission.competingMode) {
      return decline(PresentationFallback::UnsupportedCapability);
    }
    if (admission.pointerGrab || admission.touchGrab || admission.dragGrab || admission.dismissalPending) {
      return decline(PresentationFallback::InputGrab);
    }

    if (!admission.supported || request.identity == 0 || request.deadlineMsec <= request.startMsec) {
      return decline(PresentationFallback::UnsupportedCapability);
    }
    if (request.sources.empty()
        || std::ranges::any_of(request.sources, [](const auto& source) { return !source.ready(); })) {
      return decline(PresentationFallback::SourceUnavailable);
    }
    if (request.sources.size() != 2
        || request.workspaces.size() != 2
        || request.workspaces[0].empty()
        || request.workspaces[1].empty()
        || request.workspaces[0] == request.workspaces[1]
        || !std::ranges::contains(request.workspaces, request.destination)) {
      return decline(PresentationFallback::SourceUnavailable);
    }
    m_phase = PresentationPhase::Timed;
    m_request = std::move(request);
    m_progress = 0;
    m_fallback = PresentationFallback::None;
    return true;
  }

  void PresentationLease::cancel(PresentationFallback reason) {
    release();
    m_fallback = reason;
  }

  void PresentationLease::release() {
    m_request.reset();
    m_phase = PresentationPhase::Timed;
    m_progress = 0;
  }

  bool PresentationLease::advance(uint64_t nowMsec) {
    if (!m_request) {
      return false;
    }
    if (nowMsec >= m_request->deadlineMsec) {
      release();
      return true;
    }
    if (nowMsec >= m_request->startMsec) {
      m_progress = std::max(
          m_progress,
          static_cast<double>(nowMsec - m_request->startMsec)
              / static_cast<double>(m_request->deadlineMsec - m_request->startMsec)
      );
    }
    return false;
  }

  void PresentationLease::topologyChanged() {
    if (active()) {

      cancel(PresentationFallback::TopologyChanged);
    }
  }

  bool PresentationLease::gestureProgress(double progress) {
    if (!m_request || m_request->scope != PresentationScope::WorkspacePair || !std::isfinite(progress)) {
      return false;
    }
    m_progress = std::clamp(progress, 0.0, 1.0);
    return true;
  }

  std::optional<std::string> PresentationLease::endPairForRetarget() {
    if (!m_request || m_request->scope != PresentationScope::WorkspacePair) {
      return std::nullopt;
    }
    std::string destination = m_request->destination;
    release();
    return destination;
  }

  bool PresentationInputGuard::pointerButton(uint64_t device, uint32_t button, bool pressed, bool dismissPresentation) {
    const auto key = std::pair{device, button};
    if (!pressed) {
      return m_buttons.erase(key) != 0;
    }
    if (dismissPresentation || pending()) {
      m_buttons.insert(key);
      return true;
    }
    return false;
  }

  bool PresentationInputGuard::touchDown(uint64_t device, int32_t id, bool dismissPresentation) {
    if (dismissPresentation || pending()) {
      m_touches.emplace(device, id);
      return true;
    }
    return false;
  }

  bool PresentationInputGuard::touchMotion(uint64_t device, int32_t id) const {
    return m_touches.contains({device, id});
  }

  bool PresentationInputGuard::touchUp(uint64_t device, int32_t id) { return m_touches.erase({device, id}) != 0; }

  void PresentationInputGuard::touchCancel(uint64_t device) {
    std::erase_if(m_touches, [device](const auto& key) { return key.first == device; });
  }

  void PresentationInputGuard::deviceRemoved(uint64_t device) {
    touchCancel(device);
    std::erase_if(m_buttons, [device](const auto& key) { return key.first == device; });
  }

  bool PresentationInputGuard::suppressHover(bool presentationActive) const { return presentationActive || pending(); }

} // namespace umbriel
