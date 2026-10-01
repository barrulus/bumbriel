#include "check.h"
#include "scene/presentation.h"

#include <limits>

using namespace umbriel;

namespace {
  PresentationRequest request(PresentationScope scope = PresentationScope::WindowScene, uint64_t identity = 1) {
    auto source = std::make_shared<PresentationVisualSource>();
    PresentationRequest result{
        .scope = scope,
        .identity = identity,
        .startMsec = 100,
        .deadlineMsec = 300,
        .sources = {{source, source}},
        .workspaces = {"output-a:one", "output-a:two"},
        .destination = "output-a:two",
    };
    if (scope != PresentationScope::WindowScene) {
      result.sources.push_back({source, source});
    }
    return result;
  }
} // namespace

UMBRIEL_TEST(nativeReflowKeepsOriginalLifecycleDeadline) {
  PresentationLease lease;
  CHECK(lease.acquire(request()));
  CHECK(!lease.advance(150));
  CHECK_EQ(lease.progress(), 0.25);
  lease.nativeGeometryChanged();
  CHECK(lease.active());
  CHECK_EQ(lease.request()->deadlineMsec, 300U);
  CHECK(!lease.advance(200));
  CHECK_EQ(lease.progress(), 0.5);
  // A backwards test-clock reading cannot replay the opening.
  CHECK(!lease.advance(110));
  CHECK_EQ(lease.progress(), 0.5);
  // Neighbours can still be moving at 500ms; the triggering lifecycle ends now.
  CHECK(lease.advance(300));
  CHECK(!lease.needsComposedOutput());
  CHECK(!lease.finiteMotion());
}

UMBRIEL_TEST(overlapDeclinesWholeBurstWithoutRestartingRetainedClose) {
  PresentationLease lease;
  auto closing = request();
  const auto retained = closing.sources[0].display;
  CHECK(lease.acquire(std::move(closing)));
  CHECK(!lease.advance(175));
  lease.lifecycleEvent();
  CHECK(!lease.active());
  CHECK(lease.lastFallback() == PresentationFallback::OverlappingLifecycle);
  CHECK(!lease.acquire(request(PresentationScope::WindowScene, 2)));
  lease.lifecycleEvent();
  CHECK(!lease.acquire(request(PresentationScope::WindowScene, 3)));
  CHECK_EQ(retained.use_count(), 1L);
  // Only the native owner may signal that its unchanged obligations settled.
  lease.lifecycleSettled();
  CHECK(lease.acquire(request(PresentationScope::WindowScene, 4)));
  CHECK_EQ(lease.request()->identity, 4U);
}

UMBRIEL_TEST(retainedVisualOutlivesNativeCloseAndCaptureCanStartLater) {
  struct Source : PresentationVisualSource {
    explicit Source(int& destroyed) : destroyed(destroyed) {}
    ~Source() override { ++destroyed; }
    int& destroyed;
  };
  int destroyed = 0;
  PresentationLease lease;
  auto req = request();
  auto display = std::make_shared<Source>(destroyed);
  auto unfiltered = std::make_shared<Source>(destroyed);
  req.sources = {{display, unfiltered}};
  CHECK(lease.acquire(std::move(req)));
  display.reset();
  unfiltered.reset();
  CHECK_EQ(destroyed, 0);
  auto lateCapture = lease.request()->sources[0].unfiltered;
  CHECK(lease.request()->sources[0].display != lateCapture);
  lease.cancel(PresentationFallback::RendererLost);
  CHECK_EQ(destroyed, 1);
  lateCapture.reset();
  CHECK_EQ(destroyed, 2);
}

UMBRIEL_TEST(incompleteCapturePairCannotSuppressNativeRendering) {
  PresentationLease lease;
  auto req = request();
  req.sources[0].unfiltered.reset();
  CHECK(!lease.acquire(std::move(req)));
  CHECK(lease.lastFallback() == PresentationFallback::SourceUnavailable);
  CHECK(!lease.needsComposedOutput());
  CHECK(!lease.suppressHover());
}

UMBRIEL_TEST(incompleteWorkspaceCoverageCannotHideMissingFaces) {
  PresentationLease lease;
  for (auto scope : {PresentationScope::WorkspacePair, PresentationScope::WorkspaceSet}) {
    auto req = request(scope);
    req.sources.pop_back();
    CHECK(!lease.acquire(std::move(req)));
    CHECK(!lease.active());
    CHECK(lease.lastFallback() == PresentationFallback::SourceUnavailable);
    req = request(scope);
    req.sources.push_back(req.sources.front());
    CHECK(!lease.acquire(std::move(req)));
  }
  auto req = request(PresentationScope::WorkspacePair);
  req.workspaces.emplace_back("output-a:three");
  req.sources.push_back(req.sources.front());
  CHECK(!lease.acquire(std::move(req)));
}

UMBRIEL_TEST(lockedAdmissionDropsAnyExistingVisualLease) {
  PresentationLease lease;
  CHECK(lease.acquire(request()));
  const std::weak_ptr<const PresentationVisualSource> source = lease.request()->sources.front().display;
  CHECK(!lease.acquire(request(), {.locked = true}));
  CHECK(!lease.active());
  CHECK(source.expired());
  CHECK(lease.lastFallback() == PresentationFallback::Locked);
}

UMBRIEL_TEST(outputCancellationIsLocalAndReleasesVisualResources) {
  for (auto reason :
       {PresentationFallback::Locked, PresentationFallback::RendererLost, PresentationFallback::OutputRemoved,
        PresentationFallback::BindingRemoved}) {
    PresentationLease first;
    PresentationLease second;
    CHECK(first.acquire(request()));
    CHECK(second.acquire(request()));
    const std::weak_ptr<const PresentationVisualSource> visual = first.request()->sources[0].display;
    first.cancel(reason);
    CHECK(visual.expired());
    CHECK(!first.active());
    CHECK(second.active());
    CHECK(first.lastFallback() == reason);
  }
}

UMBRIEL_TEST(activeGrabsPreventAdmission) {
  for (auto admission :
       {PresentationAdmission{.pointerGrab = true}, PresentationAdmission{.touchGrab = true},
        PresentationAdmission{.dragGrab = true}, PresentationAdmission{.dismissalPending = true}}) {
    PresentationLease lease;
    CHECK(!lease.acquire(request(), admission));
    CHECK(lease.lastFallback() == PresentationFallback::InputGrab);
    CHECK(!lease.active());
  }
}

UMBRIEL_TEST(pointerDismissalConsumesPairedReleaseThenNextActivationPasses) {
  PresentationLease lease;
  PresentationInputGuard input;
  CHECK(lease.acquire(request()));
  int clientPresses = 0;
  int clientReleases = 0;
  auto button = [&](bool pressed) {
    if (input.pointerButton(7, 272, pressed, lease.active())) {
      if (pressed) {
        lease.cancel(PresentationFallback::InputDismissal);
      }
      return;
    }
    if (pressed) {
      ++clientPresses;
    } else {
      ++clientReleases;
    }
  };
  button(true);
  CHECK(!lease.active());
  CHECK(input.suppressHover(lease.active()));
  // Another device's matching release must not clear the swallowed button.
  CHECK(!input.pointerButton(8, 272, false, false));
  CHECK(input.pending());
  button(false);
  CHECK_EQ(clientPresses, 0);
  CHECK_EQ(clientReleases, 0);
  CHECK(!input.suppressHover(false));
  button(true);
  button(false);
  CHECK_EQ(clientPresses, 1);
  CHECK_EQ(clientReleases, 1);
  CHECK(!input.pending());
}

UMBRIEL_TEST(chordAndTouchDismissalConsumeWholeSequence) {
  PresentationInputGuard input;
  CHECK(input.pointerButton(7, 272, true, true));
  CHECK(input.pointerButton(7, 273, true, false));
  CHECK(input.pointerButton(7, 272, false, false));
  CHECK(input.pending());
  CHECK(input.pointerButton(7, 273, false, false));
  CHECK(!input.pending());
  CHECK(input.touchDown(4, 0, true));
  CHECK(input.touchDown(4, 1, false));
  CHECK(input.touchMotion(4, 0));
  CHECK(!input.touchMotion(5, 0));
  CHECK(input.touchUp(4, 0));
  CHECK(input.pending());
  CHECK(input.touchUp(4, 1));
  CHECK(!input.pending());
  CHECK(!input.touchDown(4, 2, false));
  CHECK(!input.touchMotion(4, 2));
  CHECK(!input.touchUp(4, 2));
}

UMBRIEL_TEST(inputDeviceRemovalDoesNotLeaveSuppressionStuck) {
  PresentationInputGuard input;
  CHECK(input.touchDown(4, 0, true));
  CHECK(input.pointerButton(7, 272, true, false));
  input.touchCancel(4);
  CHECK(input.pending());
  input.deviceRemoved(7);
  CHECK(!input.pending());
  CHECK(!input.suppressHover(false));
}

UMBRIEL_TEST(lockBetweenDismissalAndReleaseDoesNotLeakReleaseToLockClient) {
  PresentationInputGuard input;
  PresentationLease lease;
  CHECK(lease.acquire(request()));
  CHECK(input.pointerButton(1, 272, true, lease.active()));
  lease.cancel(PresentationFallback::InputDismissal);
  lease.cancel(PresentationFallback::Locked);
  CHECK(input.pointerButton(1, 272, false, false));
  CHECK(!input.pending());
  // The next full lock-client sequence is ordinary input.
  CHECK(!input.pointerButton(1, 272, true, false));
  CHECK(!input.pointerButton(1, 272, false, false));
}

UMBRIEL_TEST(carouselHeldNeedsCompositionWithoutFiniteFrameDemand) {
  PresentationLease lease;
  CHECK(lease.acquire(request(PresentationScope::WorkspaceSet)));
  CHECK(lease.finiteMotion());
  CHECK(lease.deferEmptyWorkspaceReconciliation());
  CHECK(lease.setPhase(PresentationPhase::Held));
  CHECK(lease.needsComposedOutput());
  CHECK(!lease.finiteMotion());
  CHECK(!lease.advance(100000));
  // Window migration changes face content, not the frozen inventory.
  lease.participantMigrated();
  CHECK(lease.active());
  CHECK(lease.setPhase(PresentationPhase::Settling));
  CHECK(lease.finiteMotion());
  CHECK(lease.setPhase(PresentationPhase::Held));
  CHECK(lease.setPhase(PresentationPhase::Exiting));
  CHECK(!lease.setPhase(PresentationPhase::Held));
  lease.release();
  CHECK(!lease.needsComposedOutput());
  CHECK(!lease.deferEmptyWorkspaceReconciliation());
  CHECK(!lease.suppressHover());
}

UMBRIEL_TEST(workspaceInventoryNeverTruncatesFacesOrUsesDisplayIndexAsIdentity) {
  for (int count : {1, 2, 3, 4, 5, 8, 64}) {
    PresentationInventory inventory;
    std::vector<std::string> ids;
    ids.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
      ids.push_back("output-a:" + std::to_string(index * 17 + 2));
    }
    CHECK(inventory.enter(ids, ids.front()));
    CHECK_EQ(inventory.workspaces().size(), static_cast<size_t>(count));
    PresentationLease lease;
    auto req = request(PresentationScope::WorkspaceSet);
    req.workspaces = ids;
    req.destination = ids.front();
    req.sources.resize(ids.size(), req.sources.front());
    CHECK(lease.acquire(std::move(req)));
    CHECK_EQ(lease.request()->sources.size(), ids.size());
    CHECK(lease.setPhase(PresentationPhase::Held));
    CHECK(!lease.finiteMotion());
    for (const auto& id : ids) {
      CHECK(inventory.select(id));
      CHECK_EQ(*inventory.selected(), id);
    }
    CHECK(inventory.step(1, false));
    CHECK_EQ(*inventory.selected(), ids.back());
    CHECK(inventory.step(1, true));
    CHECK_EQ(*inventory.selected(), ids.front());
    CHECK(inventory.step(-1, true));
    CHECK_EQ(*inventory.selected(), ids.back());
    CHECK(!inventory.select("unrelated-output:2"));
    CHECK_EQ(*inventory.selected(), ids.back());
    CHECK(!inventory.enter({"duplicate", "duplicate"}, "duplicate"));
    CHECK_EQ(*inventory.selected(), ids.back());
  }
}

UMBRIEL_TEST(pairReversalKeepsIdentityRetargetLandsAtAuthoritativeDestination) {
  PresentationLease lease;
  CHECK(lease.acquire(request(PresentationScope::WorkspacePair, 8)));
  CHECK(lease.gestureProgress(0.75));
  CHECK(lease.gestureProgress(0.25));
  CHECK_EQ(lease.progress(), 0.25);
  CHECK_EQ(lease.request()->identity, 8U);
  CHECK(!lease.gestureProgress(std::numeric_limits<double>::quiet_NaN()));
  CHECK_EQ(lease.progress(), 0.25);
  auto destination = lease.endPairForRetarget();
  CHECK(destination.has_value());
  CHECK_EQ(*destination, "output-a:two");
  CHECK(!lease.active());
  CHECK(lease.acquire(request(PresentationScope::WorkspacePair, 9)));
  CHECK_EQ(lease.request()->identity, 9U);
  CHECK_EQ(lease.progress(), 0.0);
}

UMBRIEL_TEST(inventoryMutationCancelsBeforeWorkspaceIdentityDisappears) {
  PresentationLease lease;
  CHECK(lease.acquire(request(PresentationScope::WorkspaceSet)));
  lease.topologyChanged();
  CHECK(!lease.active());
  CHECK(!lease.deferEmptyWorkspaceReconciliation());
  CHECK(lease.lastFallback() == PresentationFallback::TopologyChanged);
}

int main() { return RUN_TESTS(); }
