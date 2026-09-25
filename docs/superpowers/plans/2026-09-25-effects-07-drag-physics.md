# Effects Stage 7: Drag physics

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** With `[animation.windows_drag] physics = true` and the animation master switch on, a window dragged by the pointer deforms like an elastic sheet pinned at the grab point, settles after release and while held still, renders past its own box through the `drag` slot's `expand`, keeps border/overlay/window effects inside the capture, and hands a frozen deformation to the close snapshot.

**Architecture:** `DragPhysics` (pure, `src/view/drag_physics.{h,cpp}`) integrates a 4×4 spring sheet at a fixed 240 Hz step with exact grab pinning, a global displacement/velocity bound, and a slope bound that keeps the inverse lookup in the shader contractive. The move grab begins, feeds, and ends it; `View::tickAnimations` advances it on the animation clock; `View::syncAnimationShaders` binds `FX_SLOT_DRAG` with the registry's built-in deformation program, the `umbriel_deformation[16]` uniform, and an `expand` covering the excursion. The renderer knows nothing but the uniform.

**Tech Stack:** C++23, GLSL ES 1.00 built-in program, harness with `pointer_hold`.

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §2 (Drag physics), §3 (`drag` slot, capture mode with `expand`), §4 (drag physics), §6 (`drag_physics.cpp`, `480_drag_physics`). Index: `2026-09-25-effects-00-index.md`.

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Reuse the animation clock, existing move-grab lifecycle, snapshot transfer, generic uniform binder, and harness `pointer_hold`/`pointer_step`/`pointer_release`. Extract the transition-ID allocation primitive from `beginAnimationTransition` in `src/core/animation.cpp` for shared use instead of adding a second serial counter. Keep the coupled sheet solver separate from the existing scalar spring solver where their contracts differ.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- Name: drag physics. `physics` is the only key. No "wobble" anywhere.
- Default off; with it off, `DragPhysics` is never constructed into an active state and the deformation program is never compiled.
- Behaviour, not constants, is what the unit tests assert.
- Drag physics is a transient slot: while active it counts as an animation (frames, `settle` waits for it, scanout off), exactly like `windows_move`.
- Carried from Stage 1 (Task 1.4): `scene_node_update` in `wlr_scene.c` extends a node's update/damage region only by the expand of effects on the node itself or its ancestors. The drag slot lives on the content tree while the pointer moves the *frame* (`m_sceneTree`), so a frame move would leave the sheet's expand margin undamaged. Task 7.2 fixes this in umbrielfx before the drag check: when the scene has effect state, `scene_node_update` also folds in the largest expand among the updated node's descendants (`scene_subtree_effect_expand(node)`, a walk over the subtree's `scene_animation` addons, run only when `scene_effects_get(scene, false) != NULL`), and node destruction (`wlr_scene_node_destroy` → `scene_node_update` with explicit damage) damages that margin too. `480_drag_physics` asserts the trailing margin is repainted after release, which fails without it.

---

### Task 7.1: `DragPhysics`

**Files:**
- Create: `src/view/drag_physics.h`, `src/view/drag_physics.cpp`, `tests/unit/drag_physics.cpp`
- Modify: `meson.build` (`pure_sources`), `tests/meson.build`

**Interfaces:**
```cpp
// src/view/drag_physics.h
class DragPhysics {
public:
  static constexpr int kPoints = 16;
  using Sheet = std::array<std::array<float, 2>, kPoints>;
  // `grabX`/`grabY` are the grab point as fractions of the window (0-1).
  void begin(float width, float height, float grabX, float grabY);
  // Pointer delta in logical pixels since the previous call.
  void move(float dx, float dy);
  void release();
  // Advances by `seconds` at a fixed 240 Hz step. A pause over 250 ms settles immediately. Returns active().
  bool tick(double seconds);
  [[nodiscard]] bool active() const { return m_active; }
  [[nodiscard]] bool grabbed() const { return m_grabbed; }
  [[nodiscard]] uint64_t transitionId() const { return m_transitionId; }
  // Displacements divided by the window size, as the shader expects.
  [[nodiscard]] Sheet normalizedDisplacement() const;
  // Largest displacement in logical pixels, for the drawn rectangle's expand.
  [[nodiscard]] float maxDisplacement() const;
private:
  void constrain();
  Sheet m_displacement{};
  Sheet m_velocity{};
  std::array<float, kPoints> m_weights{};
  std::array<float, kPoints> m_drag{};
  float m_width = 1.0F;
  float m_height = 1.0F;
  double m_remainder = 0.0;
  uint64_t m_transitionId = 0;
  bool m_grabbed = false;
  bool m_active = false;
};
```

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/drag_physics.cpp`:

```cpp
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
        if (x < 3) largest = std::max(largest, std::abs(sheet[i + 1][axis] - sheet[i][axis]));
        if (y < 3) largest = std::max(largest, std::abs(sheet[i + 4][axis] - sheet[i][axis]));
      }
    }
    return largest;
  }
  void run(DragPhysics& physics, double seconds, double step) {
    for (double t = 0; t < seconds; t += step) {
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
```
Register `['drag-physics', ['unit/drag_physics.cpp'], []],` in `tests/meson.build` and `'src/view/drag_physics.cpp',` in `pure_sources`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'just configure >/dev/null && meson compile -C build-debug drag-physics-test 2>&1 | tail -2'`
Expected: compile error, `view/drag_physics.h` missing.

- [ ] **Step 3: Implement**

`src/view/drag_physics.cpp`:

```cpp
#include "view/drag_physics.h"

#include <algorithm>
#include <cmath>

namespace umbriel {
  namespace {
    // Spring sheet tuning. Stiffness pulls each mass home, coupling ties it
    // to its neighbours, damping bleeds energy, pointer response scales how
    // far a motion loads the sheet. Behaviour, not these numbers, is tested.
    constexpr float kStiffness = 36.0F;
    constexpr float kCoupling = 100.0F;
    constexpr float kDamping = 6.5F;
    constexpr float kPointerResponse = 2.0F;
    constexpr double kStep = 1.0 / 240.0;
    constexpr double kSettleAfterPause = 0.25;
    constexpr float kMaxDisplacementPx = 200.0F;
    constexpr float kMaxVelocity = 4000.0F;
    constexpr float kSettledDisplacement = 0.02F;
    constexpr float kSettledVelocity = 0.2F;

    uint64_t nextTransition() {
      static uint64_t serial = 0;
      return ++serial;
    }
  } // namespace

  void DragPhysics::begin(float width, float height, float grabX, float grabY) {
    *this = DragPhysics{};
    m_width = std::max(width, 1.0F);
    m_height = std::max(height, 1.0F);
    m_transitionId = nextTransition();
    m_grabbed = true;
    // One bicubic Bernstein surface couples the whole window; the shader
    // interpolates with the same weights, so the pin lands exactly on the pointer.
    const float u = std::clamp(grabX, 0.0F, 1.0F);
    const float v = std::clamp(grabY, 0.0F, 1.0F);
    const float ux = 1 - u, vy = 1 - v;
    const float horizontal[4] = {ux * ux * ux, 3 * u * ux * ux, 3 * u * u * ux, u * u * u};
    const float vertical[4] = {vy * vy * vy, 3 * v * vy * vy, 3 * v * v * vy, v * v * v};
    for (int i = 0; i < kPoints; ++i) {
      m_weights[i] = horizontal[i % 4] * vertical[i / 4];
    }
    // Spread pointer response around the grab instead of a sharp bump in the
    // nearest masses; normalise so the grab point itself does not move.
    const float x = u * 3, y = v * 3;
    float anchor = 0;
    for (int i = 0; i < kPoints; ++i) {
      const float dx = (static_cast<float>(i % 4) - x) / 3, dy = (static_cast<float>(i / 4) - y) / 3;
      m_drag[i] = std::exp(-(dx * dx + dy * dy) / 0.35F);
      anchor += m_weights[i] * m_drag[i];
    }
    for (int i = 0; i < kPoints; ++i) {
      m_drag[i] = 1 - m_drag[i] / anchor;
    }
  }

  void DragPhysics::constrain() {
    if (m_grabbed) {
      float sum = 0, position[2] = {0, 0}, velocity[2] = {0, 0};
      for (int i = 0; i < kPoints; ++i) {
        sum += m_weights[i] * m_weights[i];
        for (int axis = 0; axis < 2; ++axis) {
          position[axis] += m_weights[i] * m_displacement[i][axis];
          velocity[axis] += m_weights[i] * m_velocity[i][axis];
        }
      }
      // Pin the interpolated grab point exactly, even between grid vertices.
      for (int i = 0; i < kPoints; ++i) {
        for (int axis = 0; axis < 2; ++axis) {
          m_displacement[i][axis] -= m_weights[i] * position[axis] / sum;
          m_velocity[i][axis] -= m_weights[i] * velocity[axis] / sum;
        }
      }
    }
    // Bound adjacent slopes so the shader's inverse lookup stays a contraction
    // (no folding), then bound absolute excursion and speed. One global scale
    // preserves the pin, unlike clamping each mass.
    float ratio = 1;
    for (int axis = 0; axis < 2; ++axis) {
      float horizontal = 0, vertical = 0;
      const float extent = axis == 0 ? m_width : m_height;
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          const int i = y * 4 + x;
          if (x < 3) horizontal = std::max(horizontal, std::abs(m_displacement[i + 1][axis] - m_displacement[i][axis]));
          if (y < 3) vertical = std::max(vertical, std::abs(m_displacement[i + 4][axis] - m_displacement[i][axis]));
        }
      }
      ratio = std::max(ratio, 3 * (horizontal + vertical) / (extent * 0.7F));
    }
    for (int i = 0; i < kPoints; ++i) {
      ratio = std::max(ratio, std::abs(m_displacement[i][0]) / std::min(kMaxDisplacementPx, m_width / 5));
      ratio = std::max(ratio, std::abs(m_displacement[i][1]) / std::min(kMaxDisplacementPx, m_height / 5));
      for (int axis = 0; axis < 2; ++axis) {
        ratio = std::max(ratio, std::abs(m_velocity[i][axis]) / kMaxVelocity);
      }
    }
    if (ratio > 1) {
      for (auto& point : m_displacement) {
        point[0] /= ratio;
        point[1] /= ratio;
      }
      for (auto& point : m_velocity) {
        point[0] /= ratio;
        point[1] /= ratio;
      }
    }
  }

  void DragPhysics::move(float dx, float dy) {
    if (!m_grabbed || !std::isfinite(dx) || !std::isfinite(dy) || (dx == 0 && dy == 0)) {
      return;
    }
    for (int i = 0; i < kPoints; ++i) {
      m_displacement[i][0] -= kPointerResponse * dx * m_drag[i];
      m_displacement[i][1] -= kPointerResponse * dy * m_drag[i];
    }
    constrain();
    m_active = true;
  }

  void DragPhysics::release() { m_grabbed = false; }

  bool DragPhysics::tick(double seconds) {
    if (!m_active || !std::isfinite(seconds) || seconds <= 0) {
      return m_active;
    }
    if (seconds > kSettleAfterPause) {
      m_displacement = {};
      m_velocity = {};
      m_remainder = 0;
      m_active = false;
      return false;
    }
    m_remainder += seconds;
    while (m_remainder + 1e-12 >= kStep) {
      m_remainder -= kStep;
      Sheet acceleration{};
      for (int i = 0; i < kPoints; ++i) {
        const int neighbours[4] = {i % 4 > 0 ? i - 1 : -1, i % 4 < 3 ? i + 1 : -1, i >= 4 ? i - 4 : -1, i < 12 ? i + 4 : -1};
        for (int axis = 0; axis < 2; ++axis) {
          acceleration[i][axis] = -kStiffness * m_displacement[i][axis] - kDamping * m_velocity[i][axis];
          for (const int n : neighbours) {
            if (n >= 0) {
              acceleration[i][axis] += kCoupling * (m_displacement[n][axis] - m_displacement[i][axis]);
            }
          }
        }
      }
      for (int i = 0; i < kPoints; ++i) {
        for (int axis = 0; axis < 2; ++axis) {
          m_velocity[i][axis] += acceleration[i][axis] * static_cast<float>(kStep);
          m_displacement[i][axis] += m_velocity[i][axis] * static_cast<float>(kStep);
        }
      }
      constrain();
    }
    m_active = false;
    for (int i = 0; i < kPoints; ++i) {
      for (int axis = 0; axis < 2; ++axis) {
        m_active |= std::abs(m_displacement[i][axis]) > kSettledDisplacement
            || std::abs(m_velocity[i][axis]) > kSettledVelocity;
      }
    }
    if (!m_active) {
      m_displacement = {};
      m_velocity = {};
      m_remainder = 0;
    }
    return m_active;
  }

  DragPhysics::Sheet DragPhysics::normalizedDisplacement() const {
    Sheet sheet{};
    for (int i = 0; i < kPoints; ++i) {
      sheet[i][0] = m_displacement[i][0] / m_width;
      sheet[i][1] = m_displacement[i][1] / m_height;
    }
    return sheet;
  }

  float DragPhysics::maxDisplacement() const {
    float largest = 0;
    for (const auto& point : m_displacement) {
      largest = std::max({largest, std::abs(point[0]), std::abs(point[1])});
    }
    return largest;
  }

} // namespace umbriel
```

- [ ] **Step 4: Run the tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug drag-physics --print-errorlogs'`
Expected: all six cases pass. If `motionTrailsOppositeToThePointer` fails on the magnitude threshold, the behaviour (sign and corner order) is what matters: lower `-0.02F` to `-0.01F` rather than changing constants.

- [ ] **Step 5: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(view): drag physics spring sheet"
```

---

### Task 7.2: Deformation program, view and cursor hooks, snapshot handoff

**Files:**
- Modify: `src/scene/effect_registry.h/.cpp` (`deformationShader`), `src/view/view.h`, `src/view/view.cpp` (`enterDragPresentation` :1043, `tickAnimations` :1362-1462, `hasActiveAnimations` :1473, `animatesOn` :1464, `syncAnimationShaders`), `src/input/cursor.cpp` (`beginDrag` :1881, `processMove` :1858, `finishMove` :1930, `resetMode` :743)

**Interfaces:**
- `EffectRegistry::deformationShader()` — compiles the built-in `FX_EFFECT_ANIMATION` program on first call for the current renderer (`prepare()` warms it when `animation.enabled && windowsDrag.physics`), returns null when either switch is off.
- `View`: `void beginDragPhysics(double pointerX, double pointerY);` `void moveDragPhysics(double dx, double dy);` `void endDragPhysics();` private members `DragPhysics m_dragPhysics; uint64_t m_dragPhysicsMsec = 0;`.
- `Cursor::MoveGrab` gains `double lastX = 0; double lastY = 0;`.

- [ ] **Step 1: The built-in program**

`effect_registry.cpp`:
```cpp
    // The drag slot's built-in program. uv spans the drawn rectangle (node plus
    // expand); the inverse lookup is a contraction because DragPhysics bounds the
    // sheet's slopes. Uniform indices are constants for GLSL ES 1.00.
    constexpr const char* kDeformation = R"(uniform vec2 umbriel_deformation[16];
vec2 physics_row(float t, vec2 a, vec2 b, vec2 c, vec2 d) {
  float u = 1.0 - t;
  return u * u * (u * a + 3.0 * t * b) + t * t * (3.0 * u * c + t * d);
}
vec2 physics_offset(vec2 p) {
  p = clamp(p, 0.0, 1.0);
  vec2 a = physics_row(p.x, umbriel_deformation[0], umbriel_deformation[1], umbriel_deformation[2], umbriel_deformation[3]);
  vec2 b = physics_row(p.x, umbriel_deformation[4], umbriel_deformation[5], umbriel_deformation[6], umbriel_deformation[7]);
  vec2 c = physics_row(p.x, umbriel_deformation[8], umbriel_deformation[9], umbriel_deformation[10], umbriel_deformation[11]);
  vec2 d = physics_row(p.x, umbriel_deformation[12], umbriel_deformation[13], umbriel_deformation[14], umbriel_deformation[15]);
  return physics_row(p.y, a, b, c, d);
}
vec4 animation(vec2 uv) {
  vec2 inner = (uv - umbriel_expand) / (1.0 - 2.0 * umbriel_expand);
  vec2 source = inner;
  for (int i = 0; i < 28; i++) source = inner - physics_offset(source);
  return umbriel_sample(source * (1.0 - 2.0 * umbriel_expand) + umbriel_expand);
})";
```
```cpp
  fx_effect_shader* EffectRegistry::deformationShader() {
    const Config::Animation& settings = config().animation;
    if (!settings.enabled || !settings.windowsDrag.physics || m_renderer == nullptr) {
      return nullptr;
    }
    if (m_deformation == nullptr) {
      m_deformation = {
          fx_effect_shader_create(m_renderer, FX_EFFECT_ANIMATION, kDeformation, "animation.windows_drag"),
          fx_effect_shader_unref
      };
    }
    return m_deformation.get();
  }
```
`m_deformation` (`std::shared_ptr<fx_effect_shader>`) resets in `clear()` and on renderer change in `prepare()`; `prepare()` ends with `if (settings.enabled && settings.windowsDrag.physics) (void)deformationShader(); else m_deformation.reset();` so the program exists before the first drag render and disappears when the switch turns off.

- [ ] **Step 2: View hooks**

`view.cpp`:
```cpp
  void View::beginDragPhysics(double pointerX, double pointerY) {
    if (!config().animation.enabled || !config().animation.windowsDrag.physics) {
      return;
    }
    const wlr_box content = committedContentBox();
    if (content.width <= 0 || content.height <= 0) {
      return;
    }
    const double localX = pointerX - m_sceneTree->node.x;
    const double localY = pointerY - m_sceneTree->node.y;
    m_dragPhysics.begin(
        static_cast<float>(content.width), static_cast<float>(content.height),
        static_cast<float>(localX / content.width), static_cast<float>(localY / content.height)
    );
    m_dragPhysicsMsec = m_server->animationClockMsec();
  }

  void View::moveDragPhysics(double dx, double dy) {
    if (!m_dragPhysics.grabbed()) {
      return;
    }
    const bool wasActive = m_dragPhysics.active();
    m_dragPhysics.move(static_cast<float>(dx), static_cast<float>(dy));
    if (m_dragPhysics.active()) {
      if (!wasActive) {
        // The sheet settled while held; the idle interval since then must not count as integration time.
        m_dragPhysicsMsec = m_server->animationClockMsec();
      }
      scheduleFrame();
    }
  }

  void View::endDragPhysics() { m_dragPhysics.release(); }
```
`tickAnimations`: before `syncAnimationShaders();` add
```cpp
    if (m_dragPhysics.active()) {
      const double seconds = static_cast<double>(nowMsec - m_dragPhysicsMsec) / 1000.0;
      m_dragPhysicsMsec = nowMsec;
      active |= m_dragPhysics.tick(seconds);
    }
```
`hasActiveAnimations`: `|| m_dragPhysics.active()`. `animatesOn`: `if (m_dragPhysics.active()) return true;` first (the dragged window may span outputs).

`syncAnimationShaders`, after the `WindowsMove` binding and before `DimUnfocused`:
```cpp
    if (m_dragPhysics.active()) {
      fx_animation_parameters parameters{};
      parameters.progress = 1.0F;
      parameters.linear_progress = 1.0F;
      parameters.direction = 1.0F;
      parameters.transition_id = m_dragPhysics.transitionId();
      parameters.expand = static_cast<int>(std::ceil(m_dragPhysics.maxDisplacement())) + 2;
      if (fx_uniform* sheet = fx_parameters_add_uniform(&parameters, "umbriel_deformation", FX_UNIFORM_VEC2, DragPhysics::kPoints)) {
        const DragPhysics::Sheet displacement = m_dragPhysics.normalizedDisplacement();
        for (int i = 0; i < DragPhysics::kPoints; ++i) {
          sheet->floats[i * 2] = displacement[i][0];
          sheet->floats[i * 2 + 1] = displacement[i][1];
        }
      }
      wlr_scene_node_set_animation(&target->node, FX_SLOT_DRAG, effectRegistry().deformationShader(), &parameters);
    } else {
      wlr_scene_node_set_animation(&target->node, FX_SLOT_DRAG, nullptr, nullptr);
    }
```
(`FX_UNIFORM_VEC2 × 16 = 32` floats fits `FX_UNIFORM_FLOATS_MAX` exactly.) The close snapshot copies `FX_SLOT_DRAG` as-is (Stage 1's mapping keeps it), so a close during a drag hands the frozen deformation to the snapshot; `handleUnmap`'s `wlr_scene_node_clear_animations(&m_contentTree->node)` then clears the live view.

- [ ] **Step 3: Cursor hooks**

`cursor.cpp`:
- `beginDrag(MoveGrab& grab)`: after `grab.view->enterDragPresentation();` add `grab.view->beginDragPhysics(m_cursor->x, m_cursor->y); grab.lastX = m_cursor->x; grab.lastY = m_cursor->y;`.
- `processMove()`: after `setDragPosition(...)` add
  ```cpp
    auto* moving = std::get_if<MoveGrab>(&m_grab);
    moving->view->moveDragPhysics(m_cursor->x - moving->lastX, m_cursor->y - moving->lastY);
    moving->lastX = m_cursor->x;
    moving->lastY = m_cursor->y;
  ```
  (`grab` in that function is `const auto*`; take a mutable pointer as above.)
- `finishMove()` and `resetMode()`: before the grab is torn down, `if (auto* grab = std::get_if<MoveGrab>(&m_grab); grab != nullptr && grab->view != nullptr) grab->view->endDragPhysics();`. In `resetMode` place it before `view->restoreHomePresentation()`.

- [ ] **Step 4: Build and the drag checks**

Run: `nix develop . --command bash -c 'just build && just test && just check 4 44 45 46 47'`
Expected: green — physics is off by default so every existing drag check is unchanged.

- [ ] **Step 5: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(animation): drag physics through the drag slot"
```

---

### Task 7.3: Harness check `480_drag_physics`

**Files:**
- Create: `tests/harness/checks/480_drag_physics.sh`

- [ ] **Step 1: Write the check**

```bash
#!/usr/bin/env bash
# Drag physics deforms a held window past its own box in the direction it trails, settles back after release, and hands
# a frozen deformation to the close snapshot when the window closes mid-drag.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
readonly BTN_LEFT=272
readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/drag-physics.png"
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
backdrop = "#000000FF"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[animation]
duration_ms = 1
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = true
duration_ms = 3000
[animation.windows_drag]
physics = true
[[window_rule]]
match.title = "^physics$"
default_floating = true
[input]
mod = "super"
EOF
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF00FF00 "$UMBRIEL_UNMAP_CLIENT" physics 400 300 > "$UMBRIEL_RUNTIME_DIR/physics.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "physics")')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]]
"$UMBRIEL" settle > /dev/null
read -r x y w h id < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
green_count() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'g > 0.9 && r < 0.1 && b < 0.1' "$1"; }

# Grab the window near its top-left with Mod+drag and pull it right quickly: the far (right) edge trails, so green
# appears left of the window's new left edge, where the trailing sheet is drawn past the box.
"$UMBRIEL" clock-freeze
grab_x=$((x + 40))
grab_y=$((y + 40))
pointer_hold "$OUTPUT_W" "$OUTPUT_H" mod move "$grab_x" "$grab_y" press "$BTN_LEFT" move $((grab_x + 60)) "$grab_y" \
  move $((grab_x + 120)) "$grab_y" move $((grab_x + 180)) "$grab_y" -- release "$BTN_LEFT"
"$UMBRIEL" clock-advance 16 > /dev/null
grim "$IMAGE"
window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "physics")')
new_x=$(jq -r .x <<< "$window")
# The sheet lags: content still covers a strip left of the box's new left edge.
if (( $(green_count "8x40+$((new_x - 10))+$((y + h / 2))") < 100 )); then
  echo "no deformation trailed past the dragged window's box"
  exit 1
fi
pointer_release
# Settling after release: run the animation clock and the physics until settle succeeds; the box is rectangular again.
for _ in $(seq 40); do
  "$UMBRIEL" clock-advance 50 > /dev/null
  if timeout 0.3 "$UMBRIEL" settle > /dev/null 2>&1; then
    break
  fi
done
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "physics")')
read -r x y w h < <(jq -r '"\(.x) \(.y) \(.w) \(.h)"' <<< "$window")
if (( $(green_count "8x40+$((x - 10))+$((y + h / 2))") > 0 )); then
  echo "the sheet did not settle back into the window box after release"
  exit 1
fi
if (( $(green_count "${w}x${h}+${x}+${y}") < w * h * 9 / 10 )); then
  echo "the settled window is not drawn plainly"
  exit 1
fi

# Close during a drag: the snapshot inherits the frozen deformation and fades out with it.
grab_x=$((x + 40))
grab_y=$((y + 40))
pointer_hold "$OUTPUT_W" "$OUTPUT_H" mod move "$grab_x" "$grab_y" press "$BTN_LEFT" move $((grab_x + 60)) "$grab_y" \
  move $((grab_x + 120)) "$grab_y" move $((grab_x + 180)) "$grab_y" -- release "$BTN_LEFT"
"$UMBRIEL" clock-advance 16 > /dev/null
window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "physics")')
new_x=$(jq -r .x <<< "$window")
"$UMBRIEL" msg "window-close:$id" > /dev/null
"$UMBRIEL" clock-advance 100 > /dev/null
grim "$IMAGE"
if (( $(green_count "8x40+$((new_x - 10))+$((y + h / 2))") < 50 )); then
  echo "the close snapshot did not keep the frozen deformation"
  exit 1
fi
pointer_release
"$UMBRIEL" clock-advance 4000 > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
if (( $(green_count "$((OUTPUT_W))x$((OUTPUT_H))+0+0") > 0 )); then
  echo "the close snapshot did not retire"
  exit 1
fi
echo "drag physics deformed the held window, settled after release, and handed its deformation to the close snapshot"
```
Confirm the Mod+drag binding and modifier name in `docs/user/input.md`/`docs/user/keybinds.md` (`pointer-client`'s `mod` command holds the configured modifier; check `tests/harness/clients/pointer_client.cpp` for how it picks it) — existing drag checks (`44x`, `45x`) show the working incantation; copy theirs.

- [ ] **Step 2: Run under stress and gate the stage**

Run: `nix develop . --command bash -c 'just check 480 && just check-stress 480 8 && just format && git diff --exit-code && just lint && just test && just check'`
Expected: green.

- [ ] **Step 3: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): drag physics check"
```
