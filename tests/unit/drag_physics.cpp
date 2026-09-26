#include "view/drag_physics.h"

#include "check.h"

#include <algorithm>
#include <cmath>
#include <limits>

using umbriel::DragPhysics;

namespace {
  // Weighted displacement at the grab point, which must stay pinned.
  float pinError(const DragPhysics& physics, float grabX, float grabY) {
    const float u = grabX, v = grabY, ux = 1 - u, vy = 1 - v;
    const float horizontal[4] = {ux * ux * ux, 3 * u * ux * ux, 3 * u * u * ux, u * u * u};
    const float vertical[4] = {vy * vy * vy, 3 * v * vy * vy, 3 * v * v * vy, v * v * v};
    const auto sheet = physics.normalizedDisplacement();
    float x = 0, y = 0;
    for (int i = 0; i < DragPhysics::kPoints; ++i) {
      x += horizontal[i % 4] * vertical[i / 4] * sheet[i][0];
      y += horizontal[i % 4] * vertical[i / 4] * sheet[i][1];
    }
    return std::hypot(x, y);
  }
  // The bound `constrain()` enforces per displacement component: 3x the worst horizontal-neighbour plus
  // worst vertical-neighbour difference in that component, normalised by window size.
  float axisSlopeBound(const DragPhysics& physics, int axis) {
    const auto sheet = physics.normalizedDisplacement();
    float horizontal = 0, vertical = 0;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const int i = y * 4 + x;
        if (x < 3)
          horizontal = std::max(horizontal, std::abs(sheet[i + 1][axis] - sheet[i][axis]));
        if (y < 3)
          vertical = std::max(vertical, std::abs(sheet[i + 4][axis] - sheet[i][axis]));
      }
    }
    return 3.0F * (horizontal + vertical);
  }
  // Ticks for exactly `seconds` of simulated time at `step`; the tick count is rounded, not accumulated.
  void run(DragPhysics& physics, double seconds, double step) {
    const auto steps = static_cast<int>(std::lround(seconds / step));
    for (int i = 0; i < steps; ++i) {
      physics.tick(step);
    }
  }
} // namespace

UMBRIEL_TEST(grabPointStaysPinnedWhileTheSheetMoves) {
  DragPhysics physics;
  physics.begin(400, 300, 0.25F, 0.8F, 1);
  for (int i = 0; i < 10; ++i) {
    physics.move(12, -4);
    physics.tick(1.0 / 60);
  }
  CHECK(physics.active());
  CHECK(pinError(physics, 0.25F, 0.8F) < 0.002F);
  CHECK(physics.maxDisplacement() > 5.0F);
}

UMBRIEL_TEST(motionTrailsOppositeToThePointer) {
  DragPhysics physics;
  // Off-centre but not a grid vertex: at an exact corner (0, 0) the grab weight there is 1 and the pin
  // alone forces that corner's displacement to zero, so the check below would hold by construction.
  physics.begin(400, 300, 0.1F, 0.1F, 1);
  for (int i = 0; i < 6; ++i) {
    physics.move(30, 0);
    physics.tick(1.0 / 60);
  }
  const auto sheet = physics.normalizedDisplacement();
  // The far corner (index 15) lags behind the motion: negative x displacement, clearly more than the near corner.
  CHECK(sheet[15][0] < -0.02F);
  CHECK(std::abs(sheet[0][0]) < 0.05F);
}

UMBRIEL_TEST(displacementAndSlopeStayBoundedUnderExtremeShaking) {
  DragPhysics physics;
  // Off-centre grab: a centred grab makes the two displacement components symmetric, which hides a bound
  // enforced per component from one that only holds for their sum.
  physics.begin(120, 70, 0.25F, 0.5F, 1);
  for (int i = 0; i < 400; ++i) {
    physics.move((i % 2 == 0 ? 1 : -1) * 900.0F, (i % 3 == 0 ? 1 : -1) * 700.0F);
    physics.tick(1.0 / 240);
    const auto sheet = physics.normalizedDisplacement();
    for (const auto& point : sheet) {
      CHECK(std::isfinite(point[0]) && std::isfinite(point[1]));
      CHECK(std::abs(point[0]) <= 0.2F + 1e-4F); // width / 5
      CHECK(std::abs(point[1]) <= 0.2F + 1e-4F); // height / 5
    }
    // No folding: the inverse lookup stays a contraction when each component's own adjacent difference is
    // bounded (the two components are bounded independently, not by their sum).
    CHECK(axisSlopeBound(physics, 0) <= 0.7F + 1e-3F);
    CHECK(axisSlopeBound(physics, 1) <= 0.7F + 1e-3F);
  }
}

UMBRIEL_TEST(settlesAfterReleaseAndWhileHeldStill) {
  DragPhysics physics;
  physics.begin(400, 300, 0.5F, 0.5F, 1);
  physics.move(60, 20);
  run(physics, 0.05, 1.0 / 240);
  CHECK(physics.active());
  run(physics, 2.5, 1.0 / 120); // still held, no motion
  CHECK(!physics.active());
  CHECK(physics.maxDisplacement() == 0.0F);

  physics.begin(400, 300, 0.5F, 0.5F, 2);
  physics.move(60, 20);
  physics.release();
  CHECK(physics.active());
  run(physics, 2.5, 1.0 / 120);
  CHECK(!physics.active());
  const auto sheet = physics.normalizedDisplacement();
  for (const auto& point : sheet) {
    CHECK(point[0] == 0.0F && point[1] == 0.0F);
  }
}

UMBRIEL_TEST(integrationIsIndependentOfTheCallerFrameRate) {
  DragPhysics slow;
  DragPhysics fast;
  slow.begin(400, 300, 0.3F, 0.3F, 1);
  fast.begin(400, 300, 0.3F, 0.3F, 2);
  slow.move(40, 10);
  fast.move(40, 10);
  run(slow, 0.5, 1.0 / 60);
  run(fast, 0.5, 1.0 / 120);
  const auto a = slow.normalizedDisplacement();
  const auto b = fast.normalizedDisplacement();
  for (int i = 0; i < DragPhysics::kPoints; ++i) {
    CHECK(std::abs(a[i][0] - b[i][0]) < 1e-4F);
    CHECK(std::abs(a[i][1] - b[i][1]) < 1e-4F);
  }
}

UMBRIEL_TEST(longPausesSettleImmediatelyAndTransitionIdPassesThrough) {
  DragPhysics physics;
  physics.begin(400, 300, 0.5F, 0.5F, 42);
  CHECK_EQ(physics.transitionId(), uint64_t{42});
  physics.move(60, 20);
  CHECK(physics.active());
  CHECK(!physics.tick(2.0));
  CHECK(!physics.active());
  physics.begin(400, 300, 0.5F, 0.5F, 43);
  CHECK_EQ(physics.transitionId(), uint64_t{43});
}

UMBRIEL_TEST(tickIgnoresNonPositiveOrNonFiniteSeconds) {
  DragPhysics neverBegun;
  CHECK(!neverBegun.tick(1.0));

  DragPhysics physics;
  physics.begin(400, 300, 0.5F, 0.5F, 1);
  physics.move(60, 20);
  CHECK(physics.active());
  const auto before = physics.normalizedDisplacement();
  CHECK(physics.tick(0.0));
  CHECK(physics.tick(-1.0));
  CHECK(physics.tick(std::numeric_limits<double>::quiet_NaN()));
  CHECK(physics.tick(std::numeric_limits<double>::infinity()));
  CHECK(physics.active());
  const auto after = physics.normalizedDisplacement();
  for (int i = 0; i < DragPhysics::kPoints; ++i) {
    CHECK(before[i][0] == after[i][0]);
    CHECK(before[i][1] == after[i][1]);
  }
}

int main() { return RUN_TESTS(); }

UMBRIEL_TEST(aBorderedWindowKeepsTheGrabbedCornerUnderThePointer) {
  // The sheet spans the box the drag slot draws over: a 400x300 window inside a 24 px ring (border plus padding).
  const float boxX = -24, boxY = -24, boxWidth = 448, boxHeight = 348;
  // The pointer holds the window geometry's top-left corner, window-local (0, 0).
  const auto grab = DragPhysics::grabIn(boxX, boxY, boxWidth, boxHeight, 0.0, 0.0);
  DragPhysics physics;
  physics.begin(boxWidth, boxHeight, grab[0], grab[1], 1);
  for (int i = 0; i < 8; ++i) {
    physics.move(25, 10);
    physics.tick(1.0 / 60);
  }
  CHECK(physics.maxDisplacement() > 5.0F);
  // Drawn where the shader puts it: the box origin, plus the grab's fraction of the box, plus its displacement.
  auto at = physics.displacementAt(grab[0], grab[1]);
  CHECK(std::abs(boxX + grab[0] * boxWidth + at[0]) < 0.05F);
  CHECK(std::abs(boxY + grab[1] * boxHeight + at[1]) < 0.05F);

  // Resized with the grab at the same fraction, the deformation keeps its shape relative to the window.
  const auto before = physics.normalizedDisplacement();
  physics.resize(boxWidth * 1.5F, boxHeight * 1.25F, grab[0], grab[1]);
  const auto after = physics.normalizedDisplacement();
  for (int i = 0; i < DragPhysics::kPoints; ++i) {
    CHECK(std::abs(before[i][0] - after[i][0]) < 1e-5F && std::abs(before[i][1] - after[i][1]) < 1e-5F);
  }

  // A retarget grows the window under the same ring and keeps the pointer on the geometry's corner.
  const auto moved = DragPhysics::grabIn(boxX, boxY, 648, 448, 0.0, 0.0);
  physics.resize(648, 448, moved[0], moved[1]);
  physics.move(20, 0);
  physics.tick(1.0 / 60);
  at = physics.displacementAt(moved[0], moved[1]);
  CHECK(std::abs(boxX + moved[0] * 648 + at[0]) < 0.05F);
  CHECK(std::abs(boxY + moved[1] * 448 + at[1]) < 0.05F);
  CHECK(physics.maxDisplacement() <= physics.displacementBound() + 1e-3F);
}
