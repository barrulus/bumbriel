#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace umbriel {

  struct PresentationNativeLifecycle {
    uint64_t identity = 0;
    uint64_t startMsec = 0;
    uint64_t deadlineMsec = 0;
    uint64_t snapshot = 0;
  };

  // Internal C0 model. This is deliberately independent of the shader/config ABI
  // and does not enable scene presentation until the rendering gates are proved.
  enum class PresentationScope { WorkspacePair, WorkspaceSet, WindowScene };
  enum class PresentationPhase { Timed, Entering, Held, Settling, Exiting };
  enum class PresentationFallback {
    None,
    UnsupportedCapability,
    SourceUnavailable,
    ResourceBudget,
    CompositionFailure,
    InputGrab,
    OverlappingLifecycle,
    TopologyChanged,
    RendererLost,
    OutputRemoved,
    Locked,
    BindingRemoved,
    InputDismissal,
  };

  // Derived renderer objects retain visual resources, never a raw View or
  // CloseSnapshot. Reaping the native close cannot invalidate a retained source.
  struct PresentationVisualSource {
    virtual ~PresentationVisualSource() = default;
  };

  struct PresentationSourcePair {
    std::shared_ptr<const PresentationVisualSource> display;
    std::shared_ptr<const PresentationVisualSource> unfiltered;
    [[nodiscard]] bool ready() const { return display && unfiltered; }
  };

  struct PresentationRequest {
    PresentationScope scope = PresentationScope::WorkspacePair;
    // Caller must use nextAnimationTransitionId() (or an existing native event's
    // identity), never reuse a released identity. Reversal keeps it; retarget
    // requires a new one. The model never owns a second allocator or clock.
    uint64_t identity = 0;
    uint64_t startMsec = 0;
    uint64_t deadlineMsec = 0;
    std::vector<PresentationSourcePair> sources;
    // Stable Workspace::id() strings, in frozen face order, never display indices.
    std::vector<std::string> workspaces;
    std::string destination;
  };

  struct PresentationAdmission {
    bool supported = true;
    bool pointerGrab = false;
    bool touchGrab = false;
    bool dragGrab = false;
    bool dismissalPending = false;
    bool competingMode = false;
    bool locked = false;
  };

  // Owned per output. Native geometry, lifecycle deadlines, client ownership and
  // selection state remain with their existing owners. No inactive allocation,
  // timer, scene walk or node is needed by this model.
  class PresentationLease {
  public:
    bool acquire(PresentationRequest request, const PresentationAdmission& admission = {});
    void cancel(PresentationFallback reason);
    void release();
    // Returns true on release at the original triggering deadline. Longer
    // neighbour motion is intentionally not consulted.
    bool advance(uint64_t nowMsec);
    // A second lifecycle event declines the entire overlapping burst, until all
    // native lifecycle obligations have settled; it never queues or restarts one.
    void lifecycleEvent();
    void lifecycleSettled();
    void participantMigrated();
    void topologyChanged();
    // Reflow of existing participants is not a topology change.
    void nativeGeometryChanged() {}

    // Gesture reversal only changes progress. Timed retarget releases to the
    // returned authoritative destination before acquiring a fresh transaction.
    bool gestureProgress(double progress);
    [[nodiscard]] std::optional<std::string> endPairForRetarget();
    bool setPhase(PresentationPhase phase);

    [[nodiscard]] bool active() const { return m_request.has_value(); }
    [[nodiscard]] bool needsComposedOutput() const { return active(); }
    [[nodiscard]] bool finiteMotion() const { return active() && m_phase != PresentationPhase::Held; }
    [[nodiscard]] bool suppressHover() const { return active(); }
    [[nodiscard]] bool deferEmptyWorkspaceReconciliation() const;
    [[nodiscard]] PresentationPhase phase() const { return m_phase; }
    [[nodiscard]] double progress() const { return m_progress; }
    [[nodiscard]] PresentationFallback lastFallback() const { return m_fallback; }
    [[nodiscard]] const PresentationRequest* request() const { return m_request ? &*m_request : nullptr; }

  private:
    std::optional<PresentationRequest> m_request;
    PresentationPhase m_phase = PresentationPhase::Timed;
    PresentationFallback m_fallback = PresentationFallback::None;
    double m_progress = 0;
    bool m_overlappingLifecycle = false;
  };

  // Seat-owned pairing survives output lease destruction and pointer migration.
  // Call before delivering activation to clients or starting compositor grabs.
  // The caller cancels its visual lease when a press/down is consumed. Subsequent
  // releases/motion are swallowed even though that lease is already gone.
  class PresentationInputGuard {
  public:
    bool pointerButton(uint64_t device, uint32_t button, bool pressed, bool dismissPresentation);
    bool touchDown(uint64_t device, int32_t id, bool dismissPresentation);
    bool touchMotion(uint64_t device, int32_t id) const;
    bool touchUp(uint64_t device, int32_t id);
    void touchCancel(uint64_t device);
    void deviceRemoved(uint64_t device);
    [[nodiscard]] bool suppressHover(bool presentationActive) const;
    [[nodiscard]] bool pending() const { return !m_buttons.empty() || !m_touches.empty(); }

  private:
    std::set<std::pair<uint64_t, uint32_t>> m_buttons;
    std::set<std::pair<uint64_t, int32_t>> m_touches;
  };

  // Frozen identity inventory for modal navigation, with content reconciled by
  // the scene provider. Mutation cancels the lease before replacing this object.
  class PresentationInventory {
  public:
    bool enter(std::vector<std::string> workspaces, const std::string& selected);
    bool select(const std::string& identity);
    bool step(int direction, bool cyclic);
    [[nodiscard]] const std::vector<std::string>& workspaces() const { return m_workspaces; }
    [[nodiscard]] const std::string* selected() const;

  private:
    std::vector<std::string> m_workspaces;
    size_t m_selected = 0;
  };

} // namespace umbriel
