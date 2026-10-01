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
    if (request.scope == PresentationScope::WindowScene && m_overlappingLifecycle) {
      return decline(PresentationFallback::OverlappingLifecycle);
    }
    if (!admission.supported
        || request.identity == 0
        || (request.scope != PresentationScope::WorkspaceSet && request.deadlineMsec <= request.startMsec)) {
      return decline(PresentationFallback::UnsupportedCapability);
    }
    if (request.sources.empty()
        || std::ranges::any_of(request.sources, [](const auto& source) { return !source.ready(); })) {
      return decline(PresentationFallback::SourceUnavailable);
    }
    if (request.scope != PresentationScope::WindowScene) {
      PresentationInventory inventory;
      if (!inventory.enter(request.workspaces, request.destination)
          || request.sources.size() != request.workspaces.size()
          || (request.scope == PresentationScope::WorkspacePair && request.workspaces.size() != 2)) {
        return decline(PresentationFallback::SourceUnavailable);
      }
    }
    m_phase = request.scope == PresentationScope::WorkspaceSet ? PresentationPhase::Entering : PresentationPhase::Timed;
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
    if (!m_request || m_request->scope == PresentationScope::WorkspaceSet) {
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

  void PresentationLease::lifecycleEvent() {
    if ((m_request && m_request->scope == PresentationScope::WindowScene) || m_overlappingLifecycle) {
      m_overlappingLifecycle = true;
      cancel(PresentationFallback::OverlappingLifecycle);
    }
  }

  void PresentationLease::lifecycleSettled() { m_overlappingLifecycle = false; }

  void PresentationLease::participantMigrated() {
    if (m_request && m_request->scope == PresentationScope::WindowScene) {
      m_overlappingLifecycle = true;
      cancel(PresentationFallback::TopologyChanged);
    }
  }

  void PresentationLease::topologyChanged() {
    if (active()) {
      if (m_request->scope == PresentationScope::WindowScene) {
        m_overlappingLifecycle = true;
      }
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

  bool PresentationLease::setPhase(PresentationPhase phase) {
    if (!m_request || m_request->scope != PresentationScope::WorkspaceSet) {
      return false;
    }
    const bool valid = (m_phase == PresentationPhase::Entering && phase == PresentationPhase::Held)
        || (m_phase == PresentationPhase::Held && phase == PresentationPhase::Settling)
        || (m_phase == PresentationPhase::Settling && phase == PresentationPhase::Held)
        || (m_phase != PresentationPhase::Exiting && phase == PresentationPhase::Exiting);
    if (valid) {
      m_phase = phase;
    }
    return valid;
  }

  bool PresentationLease::deferEmptyWorkspaceReconciliation() const {
    return m_request && m_request->scope == PresentationScope::WorkspaceSet;
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

  bool PresentationInventory::enter(std::vector<std::string> workspaces, const std::string& selected) {
    if (workspaces.empty() || workspaces.size() > 64) {
      return false;
    }
    std::set<std::string> identities;
    for (const auto& identity : workspaces) {
      if (identity.empty() || !identities.insert(identity).second) {
        return false;
      }
    }
    auto it = std::ranges::find(workspaces, selected);
    if (it == workspaces.end()) {
      return false;
    }
    m_selected = static_cast<size_t>(it - workspaces.begin());
    m_workspaces = std::move(workspaces);
    return true;
  }

  bool PresentationInventory::select(const std::string& identity) {
    auto it = std::ranges::find(m_workspaces, identity);
    if (it == m_workspaces.end()) {
      return false;
    }
    m_selected = static_cast<size_t>(it - m_workspaces.begin());
    return true;
  }

  bool PresentationInventory::step(int direction, bool cyclic) {
    if (m_workspaces.empty() || (direction != -1 && direction != 1)) {
      return false;
    }
    if (direction == -1) {
      m_selected = m_selected > 0 ? m_selected - 1 : cyclic ? m_workspaces.size() - 1 : 0;
    } else {
      m_selected = m_selected + 1 < m_workspaces.size() ? m_selected + 1 : cyclic ? 0 : m_selected;
    }
    return true;
  }

  const std::string* PresentationInventory::selected() const {
    return m_workspaces.empty() ? nullptr : &m_workspaces[m_selected];
  }

} // namespace umbriel
