#include "check.h"
#include "scene/presentation.h"

#include <limits>

using namespace umbriel;

namespace {
  PresentationRequest request(PresentationScope scope = PresentationScope::WorkspacePair, uint64_t identity = 1) {
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
    result.sources.push_back({source, source});
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
  req.sources = {{display, unfiltered}, req.sources[1]};
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

int main() { return RUN_TESTS(); }
