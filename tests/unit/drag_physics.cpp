#include "view/drag_physics.h"

#include "check.h"

#include <cmath>

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
  float maxAdjacentDifference(const DragPhysics& physics, int axis) {
    const auto sheet = physics.normalizedDisplacement();
    float largest = 0;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const int i = y * 4 + x;
        if (x < 3)
          largest = std::max(largest, std::abs(sheet[i + 1][axis] - sheet[i][axis]));
        if (y < 3)
          largest = std::max(largest, std::abs(sheet[i + 4][axis] - sheet[i][axis]));
      }
    }
    return largest;
  }
  // Counted rather than accumulated: summing `step` in a float loop drifts below `seconds` by
  // rounding error at some frame rates (e.g. 30 * 1/60), adding a spurious extra tick.
  void run(DragPhysics& physics, double seconds, double step) {
    const auto steps = static_cast<int>(std::lround(seconds / step));
    for (int i = 0; i < steps; ++i) {
      physics.tick(step);
    }
  }
} // namespace

UMBRIEL_TEST(grabPointStaysPinnedWhileTheSheetMoves) {
  DragPhysics physics;
  physics.begin(400, 300, 0.25F, 0.8F);
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
  physics.begin(400, 300, 0.0F, 0.0F); // top-left corner grab
  for (int i = 0; i < 6; ++i) {
    physics.move(30, 0);
    physics.tick(1.0 / 60);
  }
  const auto sheet = physics.normalizedDisplacement();
  // The far corner (index 15) lags behind the motion: negative x displacement, clearly more than the grabbed corner.
  CHECK(sheet[15][0] < -0.02F);
  CHECK(std::abs(sheet[0][0]) < 0.002F);
}

UMBRIEL_TEST(displacementAndSlopeStayBoundedUnderExtremeShaking) {
  DragPhysics physics;
  physics.begin(120, 70, 0.5F, 0.5F);
  for (int i = 0; i < 400; ++i) {
    physics.move((i % 2 == 0 ? 1 : -1) * 900.0F, (i % 3 == 0 ? 1 : -1) * 700.0F);
    physics.tick(1.0 / 240);
    const auto sheet = physics.normalizedDisplacement();
    for (const auto& point : sheet) {
      CHECK(std::isfinite(point[0]) && std::isfinite(point[1]));
      CHECK(std::abs(point[0]) <= 0.2F + 1e-4F); // width / 5
      CHECK(std::abs(point[1]) <= 0.2F + 1e-4F); // height / 5
    }
    // No folding: the inverse lookup stays a contraction when adjacent differences are bounded.
    CHECK(3.0F * (maxAdjacentDifference(physics, 0) + maxAdjacentDifference(physics, 1)) <= 0.7F + 1e-3F);
  }
}

UMBRIEL_TEST(settlesAfterReleaseAndWhileHeldStill) {
  DragPhysics physics;
  physics.begin(400, 300, 0.5F, 0.5F);
  physics.move(60, 20);
  run(physics, 0.05, 1.0 / 240);
  CHECK(physics.active());
  run(physics, 3.0, 1.0 / 120); // still held, no motion
  CHECK(!physics.active());
  CHECK(physics.maxDisplacement() == 0.0F);

  physics.begin(400, 300, 0.5F, 0.5F);
  physics.move(60, 20);
  physics.release();
  CHECK(physics.active());
  run(physics, 3.0, 1.0 / 120);
  CHECK(!physics.active());
  const auto sheet = physics.normalizedDisplacement();
  for (const auto& point : sheet) {
    CHECK(point[0] == 0.0F && point[1] == 0.0F);
  }
}

UMBRIEL_TEST(integrationIsIndependentOfTheCallerFrameRate) {
  DragPhysics slow;
  DragPhysics fast;
  slow.begin(400, 300, 0.3F, 0.3F);
  fast.begin(400, 300, 0.3F, 0.3F);
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

UMBRIEL_TEST(longPausesSettleImmediatelyAndTransitionsAreUnique) {
  DragPhysics physics;
  physics.begin(400, 300, 0.5F, 0.5F);
  const uint64_t first = physics.transitionId();
  physics.move(60, 20);
  CHECK(physics.active());
  CHECK(!physics.tick(2.0));
  CHECK(!physics.active());
  physics.begin(400, 300, 0.5F, 0.5F);
  CHECK(physics.transitionId() != first);
}

int main() { return RUN_TESTS(); }
