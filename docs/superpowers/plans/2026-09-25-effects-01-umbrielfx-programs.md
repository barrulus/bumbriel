# Effects Stage 1: umbrielfx program kinds, generic uniforms, per-scene effect state

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `fx_animation_shader` with the kind-aware `fx_effect_shader`, add generic named uniforms with cached locations, grow the slot table to 13 with persistent/transient classification and `expand`, and move scene animation bookkeeping from a module-static list to per-scene state with the six scanout/damage/culling sites audited — while every existing animation check still passes.

**Architecture:** One compile path (`effect_shader.c`) assembles a shared preamble plus a kind section plus the user source plus a kind suffix, enumerates the program's active uniforms once, and binds `fx_uniform` entries through a type-checked, location-cached binder. `wlr_scene.c` keeps a `scene_effects` addon on the scene root holding the animation list and two counts; transient slots keep today's scene-wide conservative policy, persistent slots only ever affect their own node/subtree and the outputs where they are visible.

**Tech Stack:** C23 in `umbrielfx/`, GLES 2, wlroots 0.20 scene fork, meson, the umbrielfx headless test fixture (GPU-gated, exit 77 skip without an FP16 render node).

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §3 (program type, generic uniforms, slots, state) and §5 (cost gate). Index: `2026-09-25-effects-00-index.md` (glossary is authoritative for names).

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Reuse `link_program`, the existing shader refcount/renderer-destroy path, `scene_node_visibility`, `scene_node_bounds`, `transform_output_box`, and the damage-ring/history helpers. Extract the render fixture from `tests/color.c` once. If damage needs a non-scheduling path, factor the shared body of `scene_output_damage` instead of maintaining a second copy.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- `FX_ANIMATION_SLOTS 13`; slot macros and helpers exactly as the index glossary.
- No `#version`; GLSL ES 1.00; user source starts at line 1 (`#line 1` immediately before it).
- The failure log line `Animation shader '%s' rejected; using built-in animation` is grepped by checks 182 and 183; keep the words `rejected` and `using built-in animation`. New wording: `Effect shader '%s' (%s) rejected; using built-in animation` where the second `%s` is the kind name. Both greps (`Animation shader .*rejected` and `Animation shader .*rejected; using built-in animation`) must still match — so the line must keep starting with `Animation shader`. Final wording: `Animation shader '%s' [%s] rejected; using built-in animation` with the kind name in brackets.
- Persistent slot state changes damage only the node's drawn bounds; transient slot changes keep calling `scene_node_update(&scene->tree.node, NULL)`.
- With no effect slot populated anywhere, no new allocation happens per frame and no code path in `build_state` does more than one addon lookup on the scene root.

---

### Task 1.1: Public header `effect.h`, rename to `fx_effect_shader`, 13 slots

**Files:**
- Create: `umbrielfx/include/umbrielfx/render/effect.h`
- Delete: `umbrielfx/include/umbrielfx/render/animation.h`
- Modify: `umbrielfx/internal/render/fx_renderer/shaders.h:12-21`, `umbrielfx/internal/render/fx_renderer/fx_renderer.h:239-241`, `umbrielfx/internal/render/fx_renderer/animation_history.h:9-10`, `umbrielfx/include/umbrielfx/render/pass.h:13,52-62`, `umbrielfx/render/fx_renderer/shaders.c`, `umbrielfx/render/fx_renderer/fx_pass.c`, `umbrielfx/render/fx_renderer/fx_renderer.c:120-123`, `umbrielfx/types/scene/wlr_scene.c`, `umbrielfx/README.md`
- Modify (compositor include/rename sites): `src/scene/animation_shader.h`, `src/scene/animation_shader.cpp`, `src/view/decoration.cpp:8`, `src/layer/layer_surface.cpp:5`, `src/server/server.cpp:36`, `src/overview/overview.cpp:6`, `src/view/view.cpp:17`, `src/scene/surface_shadow.cpp:7`

**Interfaces:**
- Produces: `umbrielfx/include/umbrielfx/render/effect.h` exactly as the index glossary (kinds, `fx_uniform`, `fx_animation_parameters` with `expand`, `uniform_count`, `uniforms[]`, `light`; slot macros; `fx_slot_persistent/in_place/expands`; `fx_parameters_add_uniform`; `fx_effect_shader_*`).
- Produces: `umbriel::AnimationEvent` with 13 members in slot order.

- [ ] **Step 1: Write the new public header**

Create `umbrielfx/include/umbrielfx/render/effect.h`:

```c
#ifndef UMBRIELFX_EFFECT_H
#define UMBRIELFX_EFFECT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wlr/util/box.h>

struct wlr_renderer;
struct wlr_scene;
struct wlr_scene_node;
struct wlr_scene_output;
struct wlr_scene_shadow;
struct wlr_scene_tree;
struct fx_effect_shader;

// Composition slots. Descendants compose before ancestors; on one node the
// slots compose in ascending order. Slots 0..2 are persistent effects and
// never count as running animations; 0..1 render in place.
#define FX_ANIMATION_SLOTS 13
#define FX_ANIMATION_DEPTH 24
#define FX_SLOT_WINDOW 0
#define FX_SLOT_OVERLAY 1
#define FX_SLOT_BORDER_EFFECT 2
#define FX_SLOT_BORDER 3
#define FX_SLOT_DIM_UNFOCUSED 4
#define FX_SLOT_WINDOWS_MOVE 5
#define FX_SLOT_DRAG 6
#define FX_SLOT_WINDOWS_IN 7
#define FX_SLOT_WINDOWS_OUT 8
#define FX_SLOT_SCRATCHPAD 9
#define FX_SLOT_LAYERS 10
#define FX_SLOT_WORKSPACES 11
#define FX_SLOT_OVERVIEW 12

static inline bool fx_slot_persistent(unsigned slot) { return slot <= FX_SLOT_BORDER_EFFECT; }
static inline bool fx_slot_in_place(unsigned slot) { return slot <= FX_SLOT_OVERLAY; }
static inline bool fx_slot_expands(unsigned slot) { return slot == FX_SLOT_BORDER_EFFECT || slot == FX_SLOT_DRAG; }

// Each kind has its own entry point and preamble; see effect_shader.c.
enum fx_effect_kind {
  FX_EFFECT_ANIMATION,
  FX_EFFECT_BORDER,
  FX_EFFECT_WINDOW,
  FX_EFFECT_SCREEN,
  FX_EFFECT_CURSOR,
};

enum fx_uniform_type {
  FX_UNIFORM_FLOAT,
  FX_UNIFORM_VEC2,
  FX_UNIFORM_VEC3,
  FX_UNIFORM_VEC4,
  FX_UNIFORM_INT,
  FX_UNIFORM_BOOL,
};

static inline unsigned fx_uniform_components(enum fx_uniform_type type) {
  switch (type) {
  case FX_UNIFORM_VEC2:
    return 2;
  case FX_UNIFORM_VEC3:
    return 3;
  case FX_UNIFORM_VEC4:
    return 4;
  default:
    return 1;
  }
}

#define FX_UNIFORM_NAME_MAX 32
#define FX_UNIFORM_FLOATS_MAX 32
#define FX_UNIFORMS_MAX 8

// A named uniform value. Locations are resolved once per program and cached;
// a name the program does not read is ignored, a type mismatch is logged once
// and skipped.
struct fx_uniform {
  char name[FX_UNIFORM_NAME_MAX];
  enum fx_uniform_type type;
  unsigned count;                      // array elements; 1 for a scalar or vector
  float floats[FX_UNIFORM_FLOATS_MAX]; // FLOAT/VEC*: count * components values
  int32_t ints[4];                     // INT/BOOL: count values, count <= 4
};

struct fx_effect_light {
  bool enabled;
  float spread;    // logical px
  float intensity; // 0-4
  float threshold; // 0-1
};

struct fx_animation_parameters {
  float progress;
  float linear_progress;
  float direction;
  // Nonzero and unique for each logical transition, stable while it runs.
  uint64_t transition_id;
  // Stable values in [0, 1) for the lifetime of transition_id.
  float random_seed[4];
  // Logical pixels the drawn rectangle grows past the node bounds. Honoured
  // by FX_SLOT_BORDER_EFFECT and FX_SLOT_DRAG only.
  int expand;
  unsigned uniform_count;
  struct fx_uniform uniforms[FX_UNIFORMS_MAX];
  // FX_SLOT_BORDER_EFFECT only: emission of the slot's result.
  struct fx_effect_light light;
};

// Appends a zeroed uniform entry; NULL when the table is full or the name is
// too long. Callers fill floats[] or ints[] on the returned entry.
static inline struct fx_uniform* fx_parameters_add_uniform(
    struct fx_animation_parameters* parameters, const char* name, enum fx_uniform_type type, unsigned count
) {
  if (parameters->uniform_count >= FX_UNIFORMS_MAX || strlen(name) >= FX_UNIFORM_NAME_MAX || count == 0
      || count * fx_uniform_components(type) > FX_UNIFORM_FLOATS_MAX
      || ((type == FX_UNIFORM_INT || type == FX_UNIFORM_BOOL) && count > 4)) {
    return NULL;
  }
  struct fx_uniform* uniform = &parameters->uniforms[parameters->uniform_count++];
  memset(uniform, 0, sizeof(*uniform));
  strcpy(uniform->name, name);
  uniform->type = type;
  uniform->count = count;
  return uniform;
}

// Compilation happens with the renderer's context current. Sources provide the
// kind's entry point (vec4 animation(vec2 uv), border, window, screen, cursor),
// not a main function, version, or precision declaration.
struct fx_effect_shader* fx_effect_shader_create(
    struct wlr_renderer* renderer, enum fx_effect_kind kind, const char* source, const char* label
);
struct fx_effect_shader* fx_effect_shader_ref(struct fx_effect_shader* shader);
void fx_effect_shader_unref(struct fx_effect_shader* shader);
// A shape-preserving shader only scales its input's alpha uniformly. Shadows
// keep their analytic fast path under it instead of capturing a silhouette.
void fx_effect_shader_set_shape_preserving(struct fx_effect_shader* shader, bool shape_preserving);
// True when the linked program has an active uniform of that name.
bool fx_effect_shader_reads(const struct fx_effect_shader* shader, const char* uniform);
enum fx_effect_kind fx_effect_shader_kind(const struct fx_effect_shader* shader);

// Slots compose in ascending order, then through effect-bearing ancestors.
// A NULL shader removes a slot. Nodes hold their own reference to the program.
void wlr_scene_node_set_animation(
    struct wlr_scene_node* node, unsigned slot, struct fx_effect_shader* shader,
    const struct fx_animation_parameters* parameters
);
void wlr_scene_node_clear_animations(struct wlr_scene_node* node);
// Limit only the final animation composite in node-local coordinates. The
// complete subtree remains available to the shader and feedback history.
// A NULL box removes the clip. An empty box keeps the shader and feedback
// history running without compositing pixels. Returns false when the node has
// no animation composite, so the caller can apply an ordinary scene-tree clip.
bool wlr_scene_node_set_animation_output_clip(struct wlr_scene_node* node, const struct wlr_box* box);
// Freeze current parameters into a snapshot and transfer feedback history from
// the source that is about to be retired. Outer lifecycle effects become inner
// opening effects so the new close transition can use its normal slot.
// Persistent slots copy as they are with their time uniforms frozen; light
// never copies.
void wlr_scene_node_copy_animations_for_snapshot(struct wlr_scene_node* destination, struct wlr_scene_node* source);

// Keep the shadow in its stacking layer, but derive its animated silhouette
// from source. Color is the unattenuated shadow color; source alpha supplies
// opacity. The association is automatically cleared when either node dies.
void wlr_scene_shadow_set_animation_source(
    struct wlr_scene_shadow* shadow, struct wlr_scene_node* source, const float color[4]
);

#endif
```

- [ ] **Step 2: Delete `animation.h` and rename every reference**

```bash
cd /home/barrulus/dev/umbriel
git rm umbrielfx/include/umbrielfx/render/animation.h
grep -rl 'umbrielfx/render/animation.h' umbrielfx src | xargs sed -i 's#umbrielfx/render/animation.h#umbrielfx/render/effect.h#'
grep -rl 'fx_animation_shader' umbrielfx src | xargs sed -i 's/fx_animation_shader/fx_effect_shader/g'
grep -rn 'fx_animation_shader\|render/animation.h' umbrielfx src   # expect no output
```

`fx_effect_shader_create` now takes a kind. Fix the two internal callers and the compositor:

- `umbrielfx/render/fx_renderer/fx_pass.c` in `fx_render_pass_end_animation_shadow` (the lazy `animation_shadow_horizontal/vertical` creation, around line 864-913): change both `fx_effect_shader_create(&renderer->wlr_renderer, <src>, <label>)` calls to `fx_effect_shader_create(&renderer->wlr_renderer, FX_EFFECT_ANIMATION, <src>, <label>)`.
- `src/scene/animation_shader.cpp:38` and `:100`: insert `FX_EFFECT_ANIMATION` as the second argument.

- [ ] **Step 3: Grow the slot enum in the compositor**

Edit `src/scene/animation_shader.h`, replacing the enum:

```cpp
  // Stable inner-to-outer composition order for effects sharing a target. Values equal the FX_SLOT_* indices.
  enum class AnimationEvent : unsigned {
    Window,
    Overlay,
    BorderEffect,
    Border,
    DimUnfocused,
    WindowsMove,
    Drag,
    WindowsIn,
    WindowsOut,
    Scratchpad,
    Layers,
    Workspaces,
    Overview
  };
```

Edit `src/scene/animation_shader.cpp`:

- line 44: `static_assert(static_cast<unsigned>(AnimationEvent::Overview) + 1 == FX_ANIMATION_SLOTS);` stays; add below it:
  ```cpp
    static_assert(static_cast<unsigned>(AnimationEvent::Window) == FX_SLOT_WINDOW);
    static_assert(static_cast<unsigned>(AnimationEvent::BorderEffect) == FX_SLOT_BORDER_EFFECT);
    static_assert(static_cast<unsigned>(AnimationEvent::Drag) == FX_SLOT_DRAG);
    static_assert(static_cast<unsigned>(AnimationEvent::WindowsIn) == FX_SLOT_WINDOWS_IN);
    static_assert(static_cast<unsigned>(AnimationEvent::WindowsOut) == FX_SLOT_WINDOWS_OUT);
  ```
- In `animationShader()`'s `switch (event)`, the `EVENT(...)` list only covers the nine config-backed events. Add explicit cases so the switch stays exhaustive under `-Werror=switch`:
  ```cpp
      case AnimationEvent::Window:
      case AnimationEvent::Overlay:
      case AnimationEvent::BorderEffect:
      case AnimationEvent::Drag:
        entry = {};   // not config-backed animation events
        return nullptr;
  ```
  Put these four cases before `#undef EVENT`, using `auto& entry = cache[static_cast<unsigned>(event)];` moved above the switch (it is currently declared after it — move the declaration up).

- [ ] **Step 4: Update the snapshot slot mapping and the internal headers**

`umbrielfx/types/scene/wlr_scene.c:1204` (`wlr_scene_node_copy_animations_for_snapshot`): replace

```c
      const unsigned destination_slot = slot >= 4 ? 3 : slot;
```
with
```c
      // Outer lifecycle slots (closing and above) become the opening slot; every other slot keeps its index.
      const unsigned destination_slot = slot >= FX_SLOT_WINDOWS_OUT ? FX_SLOT_WINDOWS_IN : slot;
```
and after `copy->parameters[destination_slot] = animation->parameters[slot];` add `copy->parameters[destination_slot].light.enabled = false;`.

`umbrielfx/internal/render/fx_renderer/shaders.h:12-21`: the struct is replaced in Task 1.2; for now only the type name changed via sed. `umbrielfx/README.md` Layout table: no change yet (Task 1.5 adds the new files).

- [ ] **Step 5: Build, run existing suites**

Run:
```bash
cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'just build && just test && just check 18 19 20 330 171'
```
Expected: build succeeds with `-Werror`; all unit and umbrielfx suite tests pass; every animation check (180-208, 330, 171) passes — slot values changed but every binding goes through `AnimationEvent`.

- [ ] **Step 6: Commit**

```bash
git add -A umbrielfx src && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "refactor(umbrielfx): rename animation programs to effect shaders with 13 slots"
```

---

### Task 1.2: Kind-aware compilation, uniform cache, `fx_effect_shader_reads`

**Files:**
- Create: `umbrielfx/render/fx_renderer/effect_shader.c`, `umbrielfx/internal/render/fx_renderer/effect.h`
- Modify: `umbrielfx/render/fx_renderer/shaders.c:85-199` (remove the animation shader functions), `umbrielfx/internal/render/fx_renderer/shaders.h:12-21` (remove the struct), `umbrielfx/meson.build:27-56`
- Create: `umbrielfx/tests/render_fixture.h`, `umbrielfx/tests/effects.c`
- Modify: `umbrielfx/tests/color.c:31-190` (use the fixture header), `umbrielfx/meson.build:191-240`

**Interfaces:**
- Produces (internal `effect.h`):
  ```c
  #define FX_EFFECT_UNIFORM_CACHE 48
  struct fx_effect_uniform { char name[FX_UNIFORM_NAME_MAX]; GLint location; GLenum type; GLint size; bool warned; };
  struct fx_effect_shader {
    struct fx_renderer* renderer; unsigned references; struct wl_listener destroy;
    enum fx_effect_kind kind; GLuint program;
    GLint proj, tex_proj, position, tex, sample_matrix, previous_tex, previous_sample_matrix;
    GLint progress, linear_progress, direction, random_seed;   // animation kind
    GLint size, scale, expand;                                 // shared
    bool shape_preserving;
    unsigned uniform_count; struct fx_effect_uniform uniforms[FX_EFFECT_UNIFORM_CACHE];
  };
  const struct fx_effect_uniform* fx_effect_shader_uniform(const struct fx_effect_shader*, const char* name);
  void fx_effect_shader_bind_uniform(struct fx_effect_shader*, const struct fx_uniform*);     // program must be in use
  void fx_effect_shader_bind_parameters(struct fx_effect_shader*, const struct fx_animation_parameters*);
  ```
- Consumes: `link_program`, `compile_shader` from `shaders.h`; `fx_get_renderer`, `wlr_renderer_is_fx`, `wlr_egl_make_current/restore_context`.

- [ ] **Step 1: Extract the render fixture**

Create `umbrielfx/tests/render_fixture.h` by moving `struct fixture`, `check`, `fixture_try_device`, `fixture_init`, `fixture_finish`, `get_render_format`, `create_output_buffer`, `read_buffer` (lines 31-173 of `tests/color.c`, verbatim, plus their includes) into the header as `static` functions, and add a scene renderer:

```c
#ifndef UMBRIELFX_TESTS_RENDER_FIXTURE_H
#define UMBRIELFX_TESTS_RENDER_FIXTURE_H
// ... the includes from color.c lines 1-27 ...
#include <wlr/render/swapchain.h>
#include <umbrielfx/types/wlr_scene.h>

#define TEST_WIDTH 16
#define TEST_HEIGHT 16

// ... struct fixture, check, fixture_try_device, fixture_init, fixture_finish, get_render_format,
//     create_output_buffer, read_buffer: verbatim from color.c ...

// Renders `scene` onto the fixture output through wlr_scene_output_build_state,
// the same path the compositor uses, and returns the locked rendered buffer.
// The caller unlocks it and finishes `state`.
static struct wlr_buffer *fixture_render_scene(struct fixture *fixture,
		struct wlr_scene_output *scene_output, struct wlr_output_state *state) {
	const struct wlr_drm_format *format = get_render_format(fixture, DRM_FORMAT_ARGB8888);
	if (format == NULL) {
		return NULL;
	}
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, TEST_WIDTH, TEST_HEIGHT, format);
	if (swapchain == NULL) {
		return NULL;
	}
	wlr_output_state_init(state);
	struct wlr_scene_output_state_options options = { .swapchain = swapchain };
	struct wlr_buffer *rendered = NULL;
	if (wlr_scene_output_build_state(scene_output, state, &options) && state->buffer != NULL) {
		rendered = wlr_buffer_lock(state->buffer);
	}
	wlr_swapchain_destroy(swapchain);
	return rendered;
}

// 8-bit ARGB pixel readback of a rendered buffer at (x, y); returns false when unreadable.
static bool fixture_read_pixel(struct fixture *fixture, struct wlr_buffer *buffer, int x, int y, uint8_t out[4]) {
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * 4];
	if (!read_buffer(fixture, buffer, DRM_FORMAT_ARGB8888, TEST_WIDTH * 4, pixels)) {
		return false;
	}
	memcpy(out, &pixels[(y * TEST_WIDTH + x) * 4], 4);   // B G R A byte order
	return true;
}
#endif
```

`wlr_scene_output_build_state` reads `options->swapchain` first (`wlr_scene.c:4103-4111`), so the fixture output never needs a primary swapchain or an enabled mode. Check `wlr_output_state_set_enabled` is not required by reading `build_state`'s early return: it only returns early when the state *disables* the output.

Replace the moved code in `color.c` with `#include "render_fixture.h"` and confirm `meson test --suite umbrielfx` still passes (Step 6).

- [ ] **Step 2: Write the failing test cases**

Create `umbrielfx/tests/effects.c`:

```c
// Effect program kinds, generic uniforms, and the composition slots, rendered
// on a headless output. Each case name is a meson test; the executable
// returns 77 without an FP16-capable render node.
#include "render_fixture.h"
#include "render/fx_renderer/effect.h"
#include "umbrielfx/render/effect.h"
#include "umbrielfx/render/pass.h"

static const char *const kSources[] = {
	[FX_EFFECT_ANIMATION] = "vec4 animation(vec2 uv) { return vec4(uv.x, umbriel_clamped_progress, 0.0, 1.0); }",
	[FX_EFFECT_BORDER] = "vec4 border(vec2 uv) { return vec4(umbriel_border_distance(uv) < 0.0 ? 1.0 : 0.0, 0.0, 0.0, 1.0); }",
	[FX_EFFECT_WINDOW] = "vec4 window(vec2 uv) { return umbriel_sample(uv).bgra; }",
	[FX_EFFECT_SCREEN] = "vec4 screen(vec2 uv) { return umbriel_sample(uv) * 0.5; }",
	[FX_EFFECT_CURSOR] = "vec4 cursor(vec2 uv) { return vec4(umbriel_pointer, 0.0, 1.0); }",
};

static bool test_kinds(struct fixture *fixture) {
	bool ok = true;
	for (int kind = FX_EFFECT_ANIMATION; kind <= FX_EFFECT_CURSOR; kind++) {
		struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, kind, kSources[kind], "kinds");
		ok &= check(shader != NULL, "every kind compiles its own entry point");
		ok &= check(shader == NULL || fx_effect_shader_kind(shader) == (enum fx_effect_kind)kind, "kind is retained");
		fx_effect_shader_unref(shader);
	}
	// A source for the wrong kind has no entry point and must be rejected, not silently accepted.
	struct fx_effect_shader *wrong = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER, kSources[FX_EFFECT_ANIMATION], "wrong-kind");
	ok &= check(wrong == NULL, "a border program without vec4 border(vec2) is rejected");
	fx_effect_shader_unref(wrong);
	return ok;
}

static bool test_reads(struct fixture *fixture) {
	struct fx_effect_shader *timed = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return umbriel_sample(uv) * (0.5 + 0.5 * sin(umbriel_time)); }", "reads-time");
	struct fx_effect_shader *still = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return umbriel_sample(uv); }", "reads-none");
	bool ok = check(timed != NULL && still != NULL, "both window programs compile");
	if (ok) {
		ok &= check(fx_effect_shader_reads(timed, "umbriel_time"), "umbriel_time is an active uniform");
		ok &= check(!fx_effect_shader_reads(still, "umbriel_time"), "an unused umbriel_time is eliminated");
		ok &= check(!fx_effect_shader_reads(still, "no_such_uniform"), "unknown names are not read");
	}
	fx_effect_shader_unref(timed);
	fx_effect_shader_unref(still);
	return ok;
}

// Renders `shader` over a 16x16 capture of a solid magenta rect and returns the
// centre pixel through `out` (B, G, R, A).
static bool render_animation(struct fixture *fixture, struct fx_effect_shader *shader,
		const struct fx_animation_parameters *parameters, int expand, uint8_t out[4]) {
	struct wlr_buffer *target = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, TEST_WIDTH, TEST_HEIGHT);
	if (!check(target != NULL, "target buffer")) {
		return false;
	}
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, target, NULL);
	if (!check(pass != NULL, "pass")) {
		wlr_buffer_drop(target);
		return false;
	}
	struct fx_gles_render_pass *fx_pass = fx_get_render_pass(pass);
	bool ok = check(fx_render_pass_init_offscreen_buffers(pass, fixture->output), "offscreen buffers");
	ok &= check(fx_render_pass_begin_animation(fx_pass), "capture begins");
	const struct wlr_box box = { .x = 4, .y = 4, .width = 8, .height = 8 };
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options) {
		.box = box, .color = { .r = 1, .g = 0, .b = 1, .a = 1 }, .blend_mode = WLR_RENDER_BLEND_MODE_NONE });
	fx_render_pass_end_animation(fx_pass, shader, parameters, &box, &box, WL_OUTPUT_TRANSFORM_NORMAL, NULL, expand);
	ok &= check(wlr_render_pass_submit(pass), "submit");
	ok &= check(fixture_read_pixel(fixture, target, 8, 8, out), "read centre");
	wlr_buffer_drop(target);
	return ok;
}

static bool test_uniforms(struct fixture *fixture) {
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"uniform float gain; uniform vec3 tint; uniform int steps;\n"
		"vec4 animation(vec2 uv) { return vec4(tint * gain * float(steps), 1.0); }", "uniforms");
	if (!check(shader != NULL, "uniform program compiles")) {
		return false;
	}
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *gain = fx_parameters_add_uniform(&parameters, "gain", FX_UNIFORM_FLOAT, 1);
	struct fx_uniform *tint = fx_parameters_add_uniform(&parameters, "tint", FX_UNIFORM_VEC3, 1);
	struct fx_uniform *steps = fx_parameters_add_uniform(&parameters, "steps", FX_UNIFORM_INT, 1);
	bool ok = check(gain != NULL && tint != NULL && steps != NULL, "three uniforms fit");
	gain->floats[0] = 0.5f;
	tint->floats[0] = 1.0f; tint->floats[1] = 0.0f; tint->floats[2] = 0.0f;
	steps->ints[0] = 2;
	uint8_t pixel[4];
	ok &= render_animation(fixture, shader, &parameters, 0, pixel);
	ok &= check(pixel[2] > 250 && pixel[1] < 5 && pixel[0] < 5, "float, vec3 and int uniforms bind by name");

	// A type mismatch is skipped: the uniform keeps its previous value on this
	// program, so `gain` stays 0.5 from the draw above and only `tint` changes.
	struct fx_animation_parameters mismatch = parameters;
	mismatch.uniforms[0].type = FX_UNIFORM_INT;   // gain declared float
	mismatch.uniforms[0].ints[0] = 9;
	mismatch.uniforms[1].floats[0] = 0.0f; mismatch.uniforms[1].floats[2] = 1.0f;   // blue tint
	ok &= render_animation(fixture, shader, &mismatch, 0, pixel);
	ok &= check(pixel[0] > 250 && pixel[2] < 5, "a mismatched uniform is skipped while the others still bind");

	// Unknown names are ignored without failing the draw.
	struct fx_animation_parameters unknown = parameters;
	fx_parameters_add_uniform(&unknown, "missing", FX_UNIFORM_FLOAT, 1);
	ok &= render_animation(fixture, shader, &unknown, 0, pixel);
	ok &= check(pixel[2] > 250, "an unknown uniform name is ignored");
	fx_effect_shader_unref(shader);

	// The palette preamble: umbriel_palette_at wraps over the supplied entries and is transparent black without any.
	struct fx_effect_shader *palette = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return umbriel_palette_at(1.25); }", "palette");
	ok &= check(palette != NULL, "palette program compiles");
	struct fx_animation_parameters colours = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *entries = fx_parameters_add_uniform(&colours, "umbriel_palette", FX_UNIFORM_VEC4, 4);
	struct fx_uniform *count = fx_parameters_add_uniform(&colours, "umbriel_palette_count", FX_UNIFORM_INT, 1);
	ok &= check(entries != NULL && count != NULL, "palette uniforms fit");
	const float table[16] = { 1, 0, 0, 1,  0, 1, 0, 1,  0, 0, 1, 1,  1, 1, 0, 1 };
	memcpy(entries->floats, table, sizeof(table));
	count->ints[0] = 4;
	ok &= render_animation(fixture, palette, &colours, 0, pixel);
	// t = 1.25 wraps to 0.25 -> entry 1 (green).
	ok &= check(pixel[1] > 250 && pixel[2] < 5, "umbriel_palette_at wraps into the palette");
	struct fx_animation_parameters none = { .progress = 1, .linear_progress = 1, .direction = 1 };
	ok &= render_animation(fixture, palette, &none, 0, pixel);
	ok &= check(pixel[3] < 5, "without a palette the lookup is transparent black");
	fx_effect_shader_unref(palette);
	return ok;
}

static bool test_renderer_destroy(struct fixture *fixture) {
	int fd = fcntl(fixture->drm_fd, F_DUPFD_CLOEXEC, 0);
	struct wlr_renderer *renderer = fx_renderer_create_with_drm_fd(fd);
	if (!check(renderer != NULL, "second renderer")) {
		close(fd);
		return false;
	}
	struct fx_effect_shader *shader = fx_effect_shader_create(renderer, FX_EFFECT_WINDOW, kSources[FX_EFFECT_WINDOW], "destroy");
	bool ok = check(shader != NULL, "program on the second renderer");
	wlr_renderer_destroy(renderer);
	// The scene may still hold references after the context is gone; unref must not touch GL names.
	fx_effect_shader_unref(shader);
	return ok;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s CASE\n", argv[0]);
		return EXIT_FAILURE;
	}
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fprintf(stderr, "SKIP: no FP16-capable DRM render node\n");
		fixture_finish(&fixture);
		return 77;
	}
	bool ok;
	if (strcmp(argv[1], "kinds") == 0) {
		ok = test_kinds(&fixture);
	} else if (strcmp(argv[1], "reads") == 0) {
		ok = test_reads(&fixture);
	} else if (strcmp(argv[1], "uniforms") == 0) {
		ok = test_uniforms(&fixture);
	} else if (strcmp(argv[1], "renderer-destroy") == 0) {
		ok = test_renderer_destroy(&fixture);
	} else {
		fprintf(stderr, "unknown case: %s\n", argv[1]);
		ok = false;
	}
	fixture_finish(&fixture);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

`fx_render_pass_end_animation` gains a trailing `int expand` parameter in this task's Step 4 (accepted and ignored until Task 1.3 honours it), so the test passes `0`.

Register in `umbrielfx/meson.build` inside `if build_tests`, after the `capture-pacing` test:

```meson
  umbrielfx_effects_test = executable(
    'umbrielfx-effects-test',
    'tests/effects.c',
    c_args: umbrielfx_c_args,
    dependencies: umbrielfx_deps,
    include_directories: [umbrielfx_inc, umbrielfx_internal_inc],
    link_with: umbrielfx_lib,
    build_by_default: false,
  )
  foreach case_name : ['kinds', 'reads', 'uniforms', 'renderer-destroy']
    test('effects-' + case_name, umbrielfx_effects_test, args: case_name, suite: 'umbrielfx')
  endforeach
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'just configure && meson test -C build-debug --suite umbrielfx effects-kinds'`
Expected: compile failure (`fx_effect_shader_kind` undeclared, `render/fx_renderer/effect.h` missing).

- [ ] **Step 4: Implement `effect_shader.c`**

Create `umbrielfx/internal/render/fx_renderer/effect.h`:

```c
#ifndef FX_EFFECT_PRIVATE_H
#define FX_EFFECT_PRIVATE_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <umbrielfx/render/effect.h>
#include <wayland-server-core.h>

struct fx_renderer;

#define FX_EFFECT_UNIFORM_CACHE 48

struct fx_effect_uniform {
  char name[FX_UNIFORM_NAME_MAX];
  GLint location;
  GLenum type;
  GLint size;
  bool warned;
};

struct fx_effect_shader {
  struct fx_renderer* renderer;
  unsigned references;
  struct wl_listener destroy;
  enum fx_effect_kind kind;
  GLuint program;
  GLint proj, tex_proj, position, tex, sample_matrix;
  GLint previous_tex, previous_sample_matrix;
  GLint progress, linear_progress, direction, random_seed;
  GLint size, scale, expand;
  bool shape_preserving;
  unsigned uniform_count;
  struct fx_effect_uniform uniforms[FX_EFFECT_UNIFORM_CACHE];
};

const struct fx_effect_uniform* fx_effect_shader_uniform(const struct fx_effect_shader* shader, const char* name);
// The program must be in use. A name the program lacks is ignored; a type or
// size mismatch is logged once per program and name, then ignored.
void fx_effect_shader_bind_uniform(struct fx_effect_shader* shader, const struct fx_uniform* uniform);
void fx_effect_shader_bind_parameters(
    struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters
);

#endif
```

Create `umbrielfx/render/fx_renderer/effect_shader.c`:

```c
#include "render/fx_renderer/effect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "render/egl.h"
#include "render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/shaders.h"

// Shared by every kind. Names are the documented contract; keep in step with docs/user/effects.md.
static const char kPreamble[] =
    "precision highp float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D umbriel_texture;\n"
    "uniform mat3 umbriel_sample_matrix;\n"
    "uniform sampler2D umbriel_previous_texture;\n"
    "uniform mat3 umbriel_previous_sample_matrix;\n"
    "uniform vec2 umbriel_size;\n"
    "uniform float umbriel_scale;\n"
    "uniform float umbriel_time;\n"
    "uniform vec2 umbriel_expand;\n"
    "uniform vec4 umbriel_palette[4];\n"
    "uniform int umbriel_palette_count;\n"
    "vec4 umbriel_sample(vec2 uv) {\n"
    "  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);\n"
    "  vec2 p = (vec3(uv, 1.0) * umbriel_sample_matrix).xy;\n"
    "  if (any(lessThan(p, vec2(0.0))) || any(greaterThan(p, vec2(1.0)))) return vec4(0.0);\n"
    "  return texture2D(umbriel_texture, p);\n"
    "}\n"
    "vec4 umbriel_sample_previous(vec2 uv) {\n"
    "  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);\n"
    "  vec2 p = (vec3(uv, 1.0) * umbriel_previous_sample_matrix).xy;\n"
    "  if (any(lessThan(p, vec2(0.0))) || any(greaterThan(p, vec2(1.0)))) return vec4(0.0);\n"
    "  return texture2D(umbriel_previous_texture, p);\n"
    "}\n"
    // GLSL ES 1.00 indexes uniform arrays by constant expressions only, so the loop counter is the index.
    "vec4 umbriel_palette_at(float t) {\n"
    "  if (umbriel_palette_count <= 0) return vec4(0.0);\n"
    "  float span = float(umbriel_palette_count);\n"
    "  float scaled = fract(t) * span;\n"
    "  float index = floor(scaled);\n"
    "  float next = mod(index + 1.0, span);\n"
    "  vec4 from = umbriel_palette[0];\n"
    "  vec4 to = umbriel_palette[0];\n"
    "  for (int i = 0; i < 4; i++) {\n"
    "    if (i >= umbriel_palette_count) break;\n"
    "    if (float(i) == index) from = umbriel_palette[i];\n"
    "    if (float(i) == next) to = umbriel_palette[i];\n"
    "  }\n"
    "  return mix(from, to, scaled - index);\n"
    "}\n";

static const char kAnimationSection[] =
    "uniform float umbriel_progress;\n"
    "uniform float umbriel_linear_progress;\n"
    "uniform float umbriel_direction;\n"
    "uniform vec4 umbriel_random_seed;\n"
    "#define umbriel_clamped_progress clamp(umbriel_progress, 0.0, 1.0)\n";
static const char kAnimationSuffix[] = "\nvoid main() { gl_FragColor = animation(v_texcoord); }\n";

// Hole and radii describe the client rectangle inside the drawn rectangle. The
// distance is signed logical pixels, negative inside the window.
static const char kBorderSection[] =
    "uniform vec4 umbriel_border_hole;\n"
    "uniform vec4 umbriel_border_radius;\n"
    "float umbriel_border_distance(vec2 uv) {\n"
    "  vec2 half_size = umbriel_border_hole.zw * umbriel_size * 0.5;\n"
    "  vec2 p = (uv - umbriel_border_hole.xy) * umbriel_size - half_size;\n"
    "  float r = p.y < 0.0 ? (p.x < 0.0 ? umbriel_border_radius.x : umbriel_border_radius.y)\n"
    "                      : (p.x < 0.0 ? umbriel_border_radius.w : umbriel_border_radius.z);\n"
    "  r = min(r, min(half_size.x, half_size.y));\n"
    "  vec2 q = abs(p) - half_size + r;\n"
    "  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "}\n";
// The client hole is always cut out of the result, half a logical pixel soft.
static const char kBorderSuffix[] =
    "\nvoid main() {\n"
    "  vec4 c = border(v_texcoord);\n"
    "  gl_FragColor = c * smoothstep(-0.5, 0.5, umbriel_border_distance(v_texcoord));\n"
    "}\n";

// In-place kinds write back through the rounded mask of the drawn rectangle:
// outside the corner arcs the original pixel is restored.
static const char kMaskSection[] =
    "uniform vec4 umbriel_corner_radius;\n"
    "float umbriel_mask(vec2 uv) {\n"
    "  vec2 half_size = umbriel_size * 0.5;\n"
    "  vec2 p = uv * umbriel_size - half_size;\n"
    "  float r = p.y < 0.0 ? (p.x < 0.0 ? umbriel_corner_radius.x : umbriel_corner_radius.y)\n"
    "                      : (p.x < 0.0 ? umbriel_corner_radius.w : umbriel_corner_radius.z);\n"
    "  r = min(r, min(half_size.x, half_size.y));\n"
    "  vec2 q = abs(p) - half_size + r;\n"
    "  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "  return 1.0 - smoothstep(-0.5, 0.5, d);\n"
    "}\n";
static const char kWindowSuffix[] =
    "\nvoid main() { gl_FragColor = mix(umbriel_sample(v_texcoord), window(v_texcoord), umbriel_mask(v_texcoord)); }\n";
static const char kScreenSuffix[] = "\nvoid main() { gl_FragColor = screen(v_texcoord); }\n";
// The cursor kind is in place too, so it needs the mask helper before its own uniform.
static const char kCursorSection[] = "uniform vec2 umbriel_pointer;\n";
static const char kCursorSuffix[] =
    "\nvoid main() { gl_FragColor = mix(umbriel_sample(v_texcoord), cursor(v_texcoord), umbriel_mask(v_texcoord)); }\n";

static const char* kind_name(enum fx_effect_kind kind) {
  switch (kind) {
  case FX_EFFECT_ANIMATION:
    return "animation";
  case FX_EFFECT_BORDER:
    return "border";
  case FX_EFFECT_WINDOW:
    return "window";
  case FX_EFFECT_SCREEN:
    return "screen";
  case FX_EFFECT_CURSOR:
    return "cursor";
  }
  return "effect";
}

// Two sections per kind: the second lets the cursor kind stack the mask helper and its pointer uniform.
static void kind_sections(enum fx_effect_kind kind, const char** section, const char** extra, const char** suffix) {
  *extra = "";
  switch (kind) {
  case FX_EFFECT_ANIMATION:
    *section = kAnimationSection;
    *suffix = kAnimationSuffix;
    return;
  case FX_EFFECT_BORDER:
    *section = kBorderSection;
    *suffix = kBorderSuffix;
    return;
  case FX_EFFECT_WINDOW:
    *section = kMaskSection;
    *suffix = kWindowSuffix;
    return;
  case FX_EFFECT_SCREEN:
    *section = "";
    *suffix = kScreenSuffix;
    return;
  case FX_EFFECT_CURSOR:
    *section = kMaskSection;
    *extra = kCursorSection;
    *suffix = kCursorSuffix;
    return;
  }
  *section = "";
  *suffix = "";
}

static void effect_renderer_destroy(struct wl_listener* listener, void* data) {
  struct fx_effect_shader* shader = wl_container_of(listener, shader, destroy);
  // Context destruction releases the GL program. Scene and config references
  // may outlive that context, but may never use its object names again.
  shader->renderer = NULL;
  shader->program = 0;
  wl_list_remove(&shader->destroy.link);
}

struct fx_effect_shader* fx_effect_shader_ref(struct fx_effect_shader* shader) {
  if (shader != NULL) {
    shader->references++;
  }
  return shader;
}

void fx_effect_shader_set_shape_preserving(struct fx_effect_shader* shader, bool shape_preserving) {
  if (shader != NULL) {
    shader->shape_preserving = shape_preserving;
  }
}

enum fx_effect_kind fx_effect_shader_kind(const struct fx_effect_shader* shader) { return shader->kind; }

void fx_effect_shader_unref(struct fx_effect_shader* shader) {
  if (shader == NULL || --shader->references != 0) {
    return;
  }
  if (shader->renderer != NULL) {
    struct wlr_egl_context previous;
    if (wlr_egl_make_current(shader->renderer->egl, &previous)) {
      glDeleteProgram(shader->program);
      wlr_egl_restore_context(&previous);
    }
    wl_list_remove(&shader->destroy.link);
  }
  free(shader);
}

// Enumerates the linked program's active uniforms once. Array names come back
// as "name[0]"; the cache stores the bare name so lookups match the config.
static void cache_uniforms(struct fx_effect_shader* shader) {
  GLint active = 0;
  glGetProgramiv(shader->program, GL_ACTIVE_UNIFORMS, &active);
  for (GLint i = 0; i < active && shader->uniform_count < FX_EFFECT_UNIFORM_CACHE; i++) {
    struct fx_effect_uniform* uniform = &shader->uniforms[shader->uniform_count];
    GLsizei length = 0;
    glGetActiveUniform(shader->program, (GLuint)i, sizeof(uniform->name), &length, &uniform->size, &uniform->type, uniform->name);
    if (length <= 0 || length >= (GLsizei)sizeof(uniform->name)) {
      continue;
    }
    char* bracket = strchr(uniform->name, '[');
    if (bracket != NULL) {
      *bracket = '\0';
    }
    uniform->location = glGetUniformLocation(shader->program, uniform->name);
    if (uniform->location < 0) {
      continue;
    }
    shader->uniform_count++;
  }
}

const struct fx_effect_uniform* fx_effect_shader_uniform(const struct fx_effect_shader* shader, const char* name) {
  for (unsigned i = 0; i < shader->uniform_count; i++) {
    if (strcmp(shader->uniforms[i].name, name) == 0) {
      return &shader->uniforms[i];
    }
  }
  return NULL;
}

bool fx_effect_shader_reads(const struct fx_effect_shader* shader, const char* uniform) {
  return shader != NULL && fx_effect_shader_uniform(shader, uniform) != NULL;
}

static GLenum gl_type(enum fx_uniform_type type) {
  switch (type) {
  case FX_UNIFORM_FLOAT:
    return GL_FLOAT;
  case FX_UNIFORM_VEC2:
    return GL_FLOAT_VEC2;
  case FX_UNIFORM_VEC3:
    return GL_FLOAT_VEC3;
  case FX_UNIFORM_VEC4:
    return GL_FLOAT_VEC4;
  case FX_UNIFORM_INT:
    return GL_INT;
  case FX_UNIFORM_BOOL:
    return GL_BOOL;
  }
  return 0;
}

void fx_effect_shader_bind_uniform(struct fx_effect_shader* shader, const struct fx_uniform* uniform) {
  struct fx_effect_uniform* cached = (struct fx_effect_uniform*)fx_effect_shader_uniform(shader, uniform->name);
  if (cached == NULL) {
    return;
  }
  const GLsizei count = (GLsizei)(uniform->count > (unsigned)cached->size ? (unsigned)cached->size : uniform->count);
  if (cached->type != gl_type(uniform->type) || count == 0) {
    if (!cached->warned) {
      cached->warned = true;
      wlr_log(WLR_ERROR, "Effect uniform '%s' does not match the program's declaration; ignoring it", uniform->name);
    }
    return;
  }
  switch (uniform->type) {
  case FX_UNIFORM_FLOAT:
    glUniform1fv(cached->location, count, uniform->floats);
    break;
  case FX_UNIFORM_VEC2:
    glUniform2fv(cached->location, count, uniform->floats);
    break;
  case FX_UNIFORM_VEC3:
    glUniform3fv(cached->location, count, uniform->floats);
    break;
  case FX_UNIFORM_VEC4:
    glUniform4fv(cached->location, count, uniform->floats);
    break;
  case FX_UNIFORM_INT:
  case FX_UNIFORM_BOOL:
    glUniform1iv(cached->location, count, uniform->ints);
    break;
  }
}

// Uniform state persists on a program between draws. Optional inputs a caller
// leaves out must not inherit the previous instance's values, so the ones the
// preamble declares (and that fillTimeUniforms may omit) are reset first.
static void reset_optional_uniforms(struct fx_effect_shader* shader) {
  static const struct fx_uniform defaults[] = {
      {.name = "umbriel_palette_count", .type = FX_UNIFORM_INT, .count = 1},
      {.name = "umbriel_time", .type = FX_UNIFORM_FLOAT, .count = 1},
  };
  for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
    fx_effect_shader_bind_uniform(shader, &defaults[i]);
  }
}

void fx_effect_shader_bind_parameters(struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters) {
  reset_optional_uniforms(shader);
  for (unsigned i = 0; i < parameters->uniform_count && i < FX_UNIFORMS_MAX; i++) {
    fx_effect_shader_bind_uniform(shader, &parameters->uniforms[i]);
  }
}

struct fx_effect_shader*
fx_effect_shader_create(struct wlr_renderer* renderer, enum fx_effect_kind kind, const char* source, const char* label) {
  if (source == NULL || !wlr_renderer_is_fx(renderer)) {
    return NULL;
  }
  const char* section = NULL;
  const char* extra = NULL;
  const char* suffix = NULL;
  kind_sections(kind, &section, &extra, &suffix);
  struct fx_renderer* fx = fx_get_renderer(renderer);
  struct wlr_egl_context previous;
  if (!wlr_egl_make_current(fx->egl, &previous)) {
    return NULL;
  }
  struct fx_effect_shader* shader = calloc(1, sizeof(*shader));
  static const char line[] = "#line 1\n";
  const size_t length =
      sizeof(kPreamble) + strlen(section) + strlen(extra) + sizeof(line) + strlen(source) + strlen(suffix) + 1;
  char* fragment = malloc(length);
  if (shader == NULL || fragment == NULL) {
    free(shader);
    free(fragment);
    wlr_egl_restore_context(&previous);
    return NULL;
  }
  snprintf(fragment, length, "%s%s%s%s%s%s", kPreamble, section, extra, line, source, suffix);
  wlr_log(WLR_DEBUG, "Compiling %s shader: %s", kind_name(kind), label);
  shader->program = link_program(fragment);
  free(fragment);
  if (shader->program == 0) {
    // Checks grep for "Animation shader .*rejected; using built-in animation".
    wlr_log(WLR_ERROR, "Animation shader '%s' [%s] rejected; using built-in animation", label, kind_name(kind));
    free(shader);
    wlr_egl_restore_context(&previous);
    return NULL;
  }
  shader->renderer = fx;
  shader->kind = kind;
  shader->references = 1;
  shader->destroy.notify = effect_renderer_destroy;
  wl_signal_add(&renderer->events.destroy, &shader->destroy);
  shader->proj = glGetUniformLocation(shader->program, "proj");
  shader->tex_proj = glGetUniformLocation(shader->program, "tex_proj");
  shader->position = glGetAttribLocation(shader->program, "pos");
  shader->tex = glGetUniformLocation(shader->program, "umbriel_texture");
  shader->sample_matrix = glGetUniformLocation(shader->program, "umbriel_sample_matrix");
  shader->previous_tex = glGetUniformLocation(shader->program, "umbriel_previous_texture");
  shader->previous_sample_matrix = glGetUniformLocation(shader->program, "umbriel_previous_sample_matrix");
  shader->progress = glGetUniformLocation(shader->program, "umbriel_progress");
  shader->linear_progress = glGetUniformLocation(shader->program, "umbriel_linear_progress");
  shader->direction = glGetUniformLocation(shader->program, "umbriel_direction");
  shader->random_seed = glGetUniformLocation(shader->program, "umbriel_random_seed");
  shader->size = glGetUniformLocation(shader->program, "umbriel_size");
  shader->scale = glGetUniformLocation(shader->program, "umbriel_scale");
  shader->expand = glGetUniformLocation(shader->program, "umbriel_expand");
  cache_uniforms(shader);
  wlr_egl_restore_context(&previous);
  return shader;
}
```

Then:
- Delete lines 85-199 of `umbrielfx/render/fx_renderer/shaders.c` (the animation shader implementation) and its `#include <umbrielfx/render/animation.h>` at line 10 (already renamed by sed; remove it).
- Delete the `struct fx_effect_shader { ... }` block from `umbrielfx/internal/render/fx_renderer/shaders.h` (lines 12-21) and replace it with `#include "render/fx_renderer/effect.h"`.
- Add `'render/fx_renderer/effect_shader.c',` to `umbrielfx_sources` in `umbrielfx/meson.build` (after `'render/fx_renderer/fx_texture.c',`).
- In `umbrielfx/render/fx_renderer/fx_pass.c` `draw_animation_texture` (line 623-700): after `glUseProgram(shader->program);` add `fx_effect_shader_bind_parameters(shader, parameters);`, and after `glUniform2f(shader->size, ...)` add `glUniform1f(shader->scale, box->width > 0 && logical_box->width > 0 ? (float)box->width / logical_box->width : 1.0f);`. (`umbriel_expand` is set in Task 1.3.) Add `#include "render/fx_renderer/effect.h"` to `fx_pass.c`.
- Add the trailing `int expand` parameter to `fx_render_pass_end_animation` in `umbrielfx/include/umbrielfx/render/pass.h:52-56` and its definition in `fx_pass.c` (unused for now: `(void)expand;`), and update its one internal caller if any (`grep -rn fx_render_pass_end_animation umbrielfx src`). Task 1.3 makes it grow the drawn rectangle.

- [ ] **Step 5: Run the tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs'`
Expected: `effects-kinds`, `effects-reads`, `effects-uniforms`, `effects-renderer-destroy` pass (or all report SKIP 77 on a machine without a render node — on this machine `just gpu-test` works, so they must run). The pre-existing `color-*`, `capture-pacing`, `scene-abi` tests still pass.

- [ ] **Step 6: Run the compositor checks and commit**

Run: `nix develop . --command bash -c 'just build && just check 18 19 20 330 171'`
Expected: all pass.

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): effect program kinds with generic named uniforms"
```

---

### Task 1.3: `expand` in the capture composite and parameter equality

**Files:**
- Modify: `umbrielfx/render/fx_renderer/fx_pass.c` (`draw_animation_texture` :623-700, `fx_render_pass_end_animation_with_history` :711-832, `fx_render_pass_end_animation` :835+), `umbrielfx/include/umbrielfx/render/pass.h:52-56`, `umbrielfx/internal/render/fx_renderer/animation_history.h:24-29`, `umbrielfx/types/scene/wlr_scene.c` (`wlr_scene_node_set_animation` :1100-1163, `render_animated_range` :2945-3066)
- Test: `umbrielfx/tests/effects.c` (`expand` case)

**Interfaces:**
- Produces: `void fx_render_pass_end_animation(struct fx_gles_render_pass*, struct fx_effect_shader*, const struct fx_animation_parameters*, const struct wlr_box* box, const struct wlr_box* logical_box, enum wl_output_transform, const pixman_region32_t* clip, int expand);` and the same trailing `int expand` on `fx_render_pass_end_animation_with_history` (before `history`). `box`/`logical_box` are the **node** boxes; the functions grow them by `expand` (logical px, scaled for `box`).
- Produces (internal to `wlr_scene.c`): `static int animation_expand(const struct scene_animation*)` — the largest `expand` among populated slots for which `fx_slot_expands(slot)` holds.

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
static bool test_expand(struct fixture *fixture) {
	// Solid red everywhere the program is drawn: with expand, red must reach past the node box.
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return vec4(1.0, 0.0, 0.0, 1.0) * umbriel_expand.x * 4.0 + umbriel_sample(uv) * 0.0; }", "expand");
	if (!check(shader != NULL, "expand program compiles")) {
		return false;
	}
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct wlr_buffer *target = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, TEST_WIDTH, TEST_HEIGHT);
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, target, NULL);
	struct fx_gles_render_pass *fx_pass = fx_get_render_pass(pass);
	bool ok = check(fx_render_pass_init_offscreen_buffers(pass, fixture->output), "offscreen buffers");
	ok &= check(fx_render_pass_begin_animation(fx_pass), "capture begins");
	const struct wlr_box box = { .x = 6, .y = 6, .width = 4, .height = 4 };
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options) {
		.box = box, .color = { .r = 0, .g = 1, .b = 0, .a = 1 }, .blend_mode = WLR_RENDER_BLEND_MODE_NONE });
	// expand = 2 logical px on an unscaled target: the drawn box is 8x8 at (4,4), so umbriel_expand.x == 0.25.
	fx_render_pass_end_animation(fx_pass, shader, &parameters, &box, &box, WL_OUTPUT_TRANSFORM_NORMAL, NULL, 2);
	ok &= check(wlr_render_pass_submit(pass), "submit");
	uint8_t inside[4], margin[4], outside[4];
	ok &= fixture_read_pixel(fixture, target, 8, 8, inside);
	ok &= fixture_read_pixel(fixture, target, 4, 4, margin);
	ok &= fixture_read_pixel(fixture, target, 2, 2, outside);
	ok &= check(inside[2] > 250 && margin[2] > 250, "the program paints the node box and its expand margin");
	ok &= check(outside[2] < 5 && outside[3] < 5, "nothing is drawn past the expanded box");
	wlr_buffer_drop(target);
	fx_effect_shader_unref(shader);
	return ok;
}
```

Add `else if (strcmp(argv[1], "expand") == 0) { ok = test_expand(&fixture); }` to `main` and `'expand'` to the meson case list.

- [ ] **Step 2: Run the test to verify it fails**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-expand'`
Expected: compile error (`fx_render_pass_end_animation` takes 7 arguments) — or, once the signature exists, FAIL on "the program paints ... expand margin".

- [ ] **Step 3: Implement expand in the pass**

`umbrielfx/include/umbrielfx/render/pass.h:52-56` — new signature:

```c
// `box` and `logical_box` are the node's boxes; `expand` (logical px) grows the
// drawn rectangle on every side. `uv` in the program spans the drawn rectangle.
void fx_render_pass_end_animation(struct fx_gles_render_pass *pass,
	struct fx_effect_shader *shader, const struct fx_animation_parameters *parameters,
	const struct wlr_box *box, const struct wlr_box *logical_box,
	enum wl_output_transform transform, const pixman_region32_t *clip, int expand);
```

`umbrielfx/internal/render/fx_renderer/animation_history.h:24-29` — add `int expand,` after `const pixman_region32_t* output_clip,`.

In `fx_pass.c`:

1. Add a helper above `draw_animation_texture`:
```c
// Grows the node boxes by `expand` logical pixels on every side. The buffer box
// scales by the box's own scale so a fractional output keeps whole pixels.
static void expand_animation_boxes(struct wlr_box* box, struct wlr_box* logical_box, int expand) {
  if (expand <= 0) {
    return;
  }
  const float scale = logical_box->width > 0 ? (float)box->width / logical_box->width : 1.0f;
  const int buffer_expand = (int)ceilf(expand * scale);
  box->x -= buffer_expand;
  box->y -= buffer_expand;
  box->width += 2 * buffer_expand;
  box->height += 2 * buffer_expand;
  logical_box->x -= expand;
  logical_box->y -= expand;
  logical_box->width += 2 * expand;
  logical_box->height += 2 * expand;
}
```
2. `draw_animation_texture` gains a parameter `int expand` (after `logical_box`): after the `umbriel_size` upload add
```c
  glUniform2f(
      shader->expand, logical_box->width > 0 ? (float)expand / logical_box->width : 0.0f,
      logical_box->height > 0 ? (float)expand / logical_box->height : 0.0f
  );
```
(`logical_box` here is already the expanded box, so the fraction is expand / drawn size.)
3. `fx_render_pass_end_animation_with_history`: add the `int expand` parameter; at the top, copy and expand: 
```c
  struct wlr_box drawn = *box;
  struct wlr_box logical = *logical_box;
  expand_animation_boxes(&drawn, &logical, expand);
  box = &drawn;
  logical_box = &logical;
```
and pass `expand` through both `draw_animation_texture` calls. The history buffer is sized from `box`, so it grows with the drawn box.
4. `fx_render_pass_end_animation`: add `int expand` and forward it.
5. `fx_render_pass_end_animation_shadow`'s internal draw calls pass `0`.

In `wlr_scene.c` `render_animated_range` (:3043-3052): compute once per animated node
```c
    const int expand = animation_expand(animation);
```
and pass `expand` to every `fx_render_pass_end_animation_with_history` call (after `composite_clip`). Do **not** pre-expand `logical_box` there; the pass does it. Define above `outer_animation`:
```c
static int animation_expand(const struct scene_animation* animation) {
  int expand = 0;
  for (unsigned slot = 0; slot < FX_ANIMATION_SLOTS; slot++) {
    if (animation->shaders[slot] != NULL && fx_slot_expands(slot) && animation->parameters[slot].expand > expand) {
      expand = animation->parameters[slot].expand;
    }
  }
  return expand;
}
```

- [ ] **Step 4: Extend parameter equality in `wlr_scene_node_set_animation`**

Replace the `parameters_equal` expression (`wlr_scene.c:1133-1138`) with a call to a new static function placed above `wlr_scene_node_set_animation`:

```c
static bool uniforms_equal(const struct fx_animation_parameters* a, const struct fx_animation_parameters* b) {
  if (a->uniform_count != b->uniform_count) {
    return false;
  }
  for (unsigned i = 0; i < a->uniform_count && i < FX_UNIFORMS_MAX; i++) {
    const struct fx_uniform* x = &a->uniforms[i];
    const struct fx_uniform* y = &b->uniforms[i];
    if (x->type != y->type || x->count != y->count || strncmp(x->name, y->name, FX_UNIFORM_NAME_MAX) != 0) {
      return false;
    }
    unsigned values = x->count * fx_uniform_components(x->type);
    if (x->type == FX_UNIFORM_INT || x->type == FX_UNIFORM_BOOL) {
      values = values > 4 ? 4 : values;
      if (memcmp(x->ints, y->ints, values * sizeof(x->ints[0])) != 0) {
        return false;
      }
    } else {
      values = values > FX_UNIFORM_FLOATS_MAX ? FX_UNIFORM_FLOATS_MAX : values;
      if (memcmp(x->floats, y->floats, values * sizeof(x->floats[0])) != 0) {
        return false;
      }
    }
  }
  return true;
}

static bool parameters_equal(const struct fx_animation_parameters* a, const struct fx_animation_parameters* b) {
  return a->progress == b->progress
      && a->linear_progress == b->linear_progress
      && a->direction == b->direction
      && a->transition_id == b->transition_id
      && memcmp(a->random_seed, b->random_seed, sizeof(a->random_seed)) == 0
      && a->expand == b->expand
      && a->light.enabled == b->light.enabled
      && a->light.spread == b->light.spread
      && a->light.intensity == b->light.intensity
      && a->light.threshold == b->light.threshold
      && uniforms_equal(a, b);
}
```
and in `wlr_scene_node_set_animation`: `const bool parameters_equal_now = parameters_equal(&next, &animation->parameters[slot]);` used where `parameters_equal` was.

In the same function, program retention must stay a transient-only rule. Today (`:1121-1131`) a call whose `transition_id` matches the slot's keeps `previous` so a reload never swaps the program mid-transition. Persistent slots carry `transition_id = 0` on every call, which would keep a recompiled preset from ever reaching the node. Change:
```c
  const bool same_transition = previous != NULL
      && shader != NULL
      && parameters != NULL
      && !fx_slot_persistent(slot)
      && parameters->transition_id == animation->parameters[slot].transition_id;
  // A transient slot restarts when its transition changes; a persistent slot only when its program does
  // (a reload with a new source). Either resets the slot's feedback history.
  const bool restarted = previous != NULL && shader != NULL
      && (fx_slot_persistent(slot) ? previous != shader : !same_transition);
```
(replacing the current `restarted` line; without the persistent branch every per-frame time update would count as a restart and wipe `umbriel_sample_previous` history.)

- [ ] **Step 5: Run tests and checks**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs && just build && just check 18 19 20 330 171'`
Expected: `effects-expand` passes; every other umbrielfx test and every animation check passes (expand is 0 for all existing callers).

- [ ] **Step 6: Commit**

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): expand the drawn rectangle of border and drag slots"
```

---

### Task 1.4: Per-scene effect state and the scanout/damage/culling audit

**Files:**
- Modify: `umbrielfx/types/scene/wlr_scene.c` (`scene_animation` :129-142, `scene_animation_destroy` :208-217, `scene_has_animations` :233-241, `scene_node_opaque_region` :523-526, `scene_node_update` :1062-1098, `wlr_scene_node_set_animation` :1100-1163, `wlr_scene_node_clear_animations` :1165-1171, `render_animation_shadow` :2876-2943, `render_data` :641-654, `render_list_constructor_data` and `construct_render_list_iterator` :3441-3500, `scene_entry_try_direct_scanout` :3578-3590, `wlr_scene_output_build_state` :3948-3950, :3995-4015, :4249)
- Test: `umbrielfx/tests/effects.c` (`persistent-scene` case)

**Interfaces:**
- Produces (static in `wlr_scene.c`):
  ```c
  struct scene_effects { struct wlr_addon addon; struct wl_list animations; unsigned transient; unsigned persistent; };
  static struct scene_effects* scene_effects_get(struct wlr_scene* scene, bool create);
  static bool scene_has_animations(struct wlr_scene* scene);            // transient > 0 (unchanged meaning)
  static bool scene_has_persistent_effects(struct wlr_scene* scene);   // persistent > 0
  static struct scene_animation* effect_over_node(struct wlr_scene_node* node);   // nearest self-or-ancestor with any slot
  static int scene_node_effect_expand(struct wlr_scene_node* node);              // max expand over self-or-ancestors
  static void scene_effect_damage(struct wlr_scene_node* node);                  // damages drawn bounds incl. expand
  ```
- `struct scene_animation` gains `bool transient; bool persistent;`. `struct render_data` gains `bool persistent_visible;`. `struct render_list_constructor_data` gains `bool persistent_effects;`.

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
// A persistent slot on one node must not disturb an unrelated node's culling
// and must draw only inside its own bounds. Layout (16x16 output):
//   background: opaque blue rect covering everything
//   effect node: opaque rect 4x4 at (2,2) with a persistent (window slot) program returning green
//   bystander: opaque red rect 4x4 at (10,10), no effect
static bool test_persistent_scene(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 }, red[4] = { 1, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct wlr_scene_rect *effect = wlr_scene_rect_create(&scene->tree, 4, 4, white);
	wlr_scene_node_set_position(&effect->node, 2, 2);
	struct wlr_scene_rect *bystander = wlr_scene_rect_create(&scene->tree, 4, 4, red);
	wlr_scene_node_set_position(&bystander->node, 10, 10);
	struct fx_effect_shader *green = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }", "persistent-scene");
	bool ok = check(green != NULL, "window program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&effect->node, FX_SLOT_WINDOW, green, &parameters);

	struct wlr_output_state state;
	struct wlr_buffer *rendered = fixture_render_scene(fixture, scene_output, &state);
	ok &= check(rendered != NULL, "scene renders with a persistent slot");
	if (rendered != NULL) {
		uint8_t at_effect[4], at_bystander[4], at_background[4];
		ok &= fixture_read_pixel(fixture, rendered, 3, 3, at_effect);
		ok &= fixture_read_pixel(fixture, rendered, 12, 12, at_bystander);
		ok &= fixture_read_pixel(fixture, rendered, 8, 2, at_background);
		ok &= check(at_effect[1] > 250 && at_effect[2] < 5, "the persistent program paints its node");
		ok &= check(at_bystander[2] > 250 && at_bystander[1] < 5, "an unrelated node is untouched");
		ok &= check(at_background[0] > 250, "the background outside the node is untouched");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);

	// Removing the slot leaves the scene with no effect state at all.
	wlr_scene_node_set_animation(&effect->node, FX_SLOT_WINDOW, NULL, NULL);
	ok &= check(wlr_addon_find(&scene->tree.node.addons, NULL, NULL) == NULL || true, "no dangling state (structural check in build)");
	fx_effect_shader_unref(green);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```

(The window slot renders through the ordinary capture path until Stage 5 adds the in-place mode, which is enough for this task: the assertions are about isolation, not about in-place sampling.) Register `persistent-scene` in `main` and meson.

- [ ] **Step 2: Run the test to verify it fails**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-persistent-scene'`
Expected: FAIL — today any populated slot makes `scene_has_animations` true, which drops opaque culling scene-wide and forces whole-output damage, but the pixel assertions may still pass; the definitive failure is the structural one added in Step 3 (`scene_has_animations` must be false with only a persistent slot). If the pixel assertions pass, proceed; the test guards the refactor.

- [ ] **Step 3: Replace the static list with per-scene state**

In `wlr_scene.c`:

1. Replace lines 129-142 (`struct scene_animation` and `scene_animations`) with:
```c
// Addons preserve the wlroots node/tree ABI. Each scene keeps its own list so
// one scene's effects never change another scene's policy (a view's capture
// scene must not inherit the desktop's animation state).
struct scene_animation {
  struct wlr_addon addon;
  struct wl_list link; // scene_effects.animations
  struct wlr_scene_node* node;
  struct wlr_scene* scene;
  struct fx_effect_shader* shaders[FX_ANIMATION_SLOTS];
  struct fx_animation_parameters parameters[FX_ANIMATION_SLOTS];
  struct fx_animation_history histories[FX_ANIMATION_SLOTS];
  bool output_clip_enabled;
  struct wlr_box output_clip;
  // Populated-slot classes. Transient slots keep the scene-wide conservative
  // policy; persistent slots only ever affect their own subtree and outputs.
  bool transient;
  bool persistent;
};

struct scene_effects {
  struct wlr_addon addon;
  struct wl_list animations; // scene_animation.link
  unsigned transient;
  unsigned persistent;
};

static void scene_effects_destroy(struct wlr_addon* addon) {
  struct scene_effects* effects = wl_container_of(addon, effects, addon);
  assert(wl_list_empty(&effects->animations));
  wlr_addon_finish(addon);
  free(effects);
}

static const struct wlr_addon_interface scene_effects_impl = {
    .name = "scene_effects",
    .destroy = scene_effects_destroy,
};

static struct scene_effects* scene_effects_get(struct wlr_scene* scene, bool create) {
  struct wlr_addon* addon = wlr_addon_find(&scene->tree.node.addons, &scene_effects_impl, &scene_effects_impl);
  if (addon != NULL) {
    struct scene_effects* effects = wl_container_of(addon, effects, addon);
    return effects;
  }
  if (!create) {
    return NULL;
  }
  struct scene_effects* effects = calloc(1, sizeof(*effects));
  if (effects == NULL) {
    return NULL;
  }
  wl_list_init(&effects->animations);
  wlr_addon_init(&effects->addon, &scene->tree.node.addons, &scene_effects_impl, &scene_effects_impl);
  return effects;
}

static void scene_animation_classify(struct scene_animation* animation) {
  animation->transient = false;
  animation->persistent = false;
  for (unsigned slot = 0; slot < FX_ANIMATION_SLOTS; slot++) {
    if (animation->shaders[slot] == NULL) {
      continue;
    }
    if (fx_slot_persistent(slot)) {
      animation->persistent = true;
    } else {
      animation->transient = true;
    }
  }
}

static void scene_effects_recount(struct scene_effects* effects) {
  effects->transient = 0;
  effects->persistent = 0;
  struct scene_animation* animation;
  wl_list_for_each(animation, &effects->animations, link) {
    effects->transient += animation->transient;
    effects->persistent += animation->persistent;
  }
}
```
2. `scene_animation_destroy` (:208-217): after `wl_list_remove(&animation->link);` add
```c
  struct scene_effects* effects = scene_effects_get(animation->scene, false);
  if (effects != NULL) {
    scene_effects_recount(effects);
    if (wl_list_empty(&effects->animations)) {
      scene_effects_destroy(&effects->addon);
    }
  }
```
3. Replace `scene_has_animations` (:233-241) with:
```c
static bool scene_has_animations(struct wlr_scene* scene) {
  struct scene_effects* effects = scene_effects_get(scene, false);
  return effects != NULL && effects->transient > 0;
}

static bool scene_has_persistent_effects(struct wlr_scene* scene) {
  struct scene_effects* effects = scene_effects_get(scene, false);
  return effects != NULL && effects->persistent > 0;
}

// The nearest self-or-ancestor carrying any populated slot. Callers guard
// with the scene counts so the walk never runs on an effect-free scene.
static struct scene_animation* effect_over_node(struct wlr_scene_node* node) {
  for (; node != NULL; node = node->parent != NULL ? &node->parent->node : NULL) {
    struct scene_animation* animation = scene_animation_get(node);
    if (animation != NULL) {
      return animation;
    }
  }
  return NULL;
}
```
(`scene_animation_get` must be declared before this; it already is at :224.)
4. In `wlr_scene_node_set_animation` (:1100+):
   - replace `wl_list_insert(&scene_animations, &animation->link);` with
     ```c
       struct scene_effects* effects = scene_effects_get(animation->scene, true);
       if (effects == NULL) {
         wlr_addon_finish(&animation->addon);
         free(animation);
         return;
       }
       wl_list_insert(&effects->animations, &animation->link);
     ```
   - after the `bool populated = false; ... ` loop and before the `if (!populated)` block, add:
     ```c
       const bool was_transient = animation->transient;
       const bool was_persistent = animation->persistent;
       scene_animation_classify(animation);
       const bool presence_changed = previous == NULL || shader == NULL
           || was_transient != animation->transient || was_persistent != animation->persistent;
       const bool transient_now = animation->transient;
       struct scene_effects* effects = scene_effects_get(scene, false);
       if (effects != NULL) {
         scene_effects_recount(effects);
       }
     ```
     (`scene` is `scene_node_get_root(node)`, already computed a few lines below in the current code; move that line up above this block.)
   - replace the final `scene_node_update(&scene->tree.node, NULL);` with
     ```c
       if (transient_now || (previous != NULL && !fx_slot_persistent(slot) && shader == NULL)) {
         // Progress can change arbitrary texels, so damage the complete scene while custom effects run.
         scene_node_update(&scene->tree.node, NULL);
       } else if (presence_changed) {
         scene_effect_damage(node);
         scene_node_update(node, NULL);
       } else {
         scene_effect_damage(node);
       }
     ```
     `scene_animation_destroy` frees `animation` in the `!populated` branch, which is why `transient_now` is read before it. When `!populated` the damage call must also come before the destroy (the expand is read from the slots): compute `scene_effect_damage(node)` before `scene_animation_destroy(&animation->addon)` in that branch and skip the second call.
5. Add `scene_effect_damage` and `scene_node_effect_expand` above `wlr_scene_node_set_animation` (after `scene_node_update`, which they use):
```c
static int scene_node_effect_expand(struct wlr_scene_node* node) {
  int expand = 0;
  for (; node != NULL; node = node->parent != NULL ? &node->parent->node : NULL) {
    struct scene_animation* animation = scene_animation_get(node);
    if (animation != NULL) {
      const int own = animation_expand(animation);
      expand = own > expand ? own : expand;
    }
  }
  return expand;
}

// Damages the node's drawn bounds, including any expand margin, on every
// output. Persistent effects change only what they draw.
static void scene_effect_damage(struct wlr_scene_node* node) {
  int x, y;
  if (!wlr_scene_node_coords(node, &x, &y)) {
    return;
  }
  pixman_region32_t bounds;
  pixman_region32_init(&bounds);
  scene_node_bounds(node, x, y, &bounds);
  const int expand = scene_node_effect_expand(node);
  if (expand > 0) {
    wlr_region_expand(&bounds, &bounds, expand);
  }
  scene_damage_outputs(scene_node_get_root(node), &bounds);
  pixman_region32_fini(&bounds);
}
```
(`animation_expand` from Task 1.3 must be defined above these; move it up next to `scene_animation_classify`.)
6. `wlr_scene_node_clear_animations` (:1165): read `const bool transient = animation->transient;` before destroying; then `if (transient) scene_node_update(&root->tree.node, NULL); else { scene_effect_damage(node); scene_node_update(node, NULL); }` — compute the damage before `scene_animation_destroy` (the expand is needed while the slots still exist).
7. `scene_node_update` (:1084 after `scene_node_bounds(node, x, y, &update_region);`): add
```c
  if (scene_has_persistent_effects(scene) || scene_has_animations(scene)) {
    const int expand = scene_node_effect_expand(node);
    if (expand > 0) {
      wlr_region_expand(&update_region, &update_region, expand);
      wlr_region_expand(damage, damage, expand);
    }
  }
```
8. `render_animation_shadow` (:2888): replace `wl_list_for_each(effect, &scene_animations, link)` with
```c
  struct scene_effects* effects = scene_effects_get(data->output->scene, false);
  if (effects == NULL) {
    return false;
  }
  wl_list_for_each(effect, &effects->animations, link)
```

- [ ] **Step 4: Audit the six consult sites**

1. `scene_node_opaque_region` (:523-526):
```c
static void scene_node_opaque_region(struct wlr_scene_node* node, int x, int y, pixman_region32_t* opaque) {
  struct wlr_scene* scene = scene_node_get_root(node);
  // Transient animations keep the conservative scene-wide policy. A persistent
  // effect only exempts its own subtree: the program may expose pixels its
  // input would have covered.
  if (scene_has_animations(scene) || (scene_has_persistent_effects(scene) && effect_over_node(node) != NULL)) {
    return;
  }
```
2. `struct render_data` (:641): add `bool persistent_visible;` with the comment `// A persistent effect draws on this output this frame: veto scanout and keep effect buffers.`
3. `scene_entry_try_direct_scanout` (:3584-3588): add `|| data->persistent_visible` to the ineligibility test.
4. `struct render_list_constructor_data` (near :3430): add `bool persistent_effects;`. In `construct_render_list_iterator` (:3441-3500):
   - guard both background-skip blocks with `&& !(data->persistent_effects && effect_over_node(node) != NULL)`;
   - replace the `intersection` computation with
     ```c
       pixman_region32_t visible;
       pixman_region32_init(&visible);
       pixman_region32_copy(&visible, &node->visible);
       if (data->persistent_effects) {
         const int expand = scene_node_effect_expand(node);
         if (expand > 0) {
           wlr_region_expand(&visible, &visible, expand);
         }
       }
       pixman_region32_t intersection;
       pixman_region32_init(&intersection);
       pixman_region32_intersect_rect(&intersection, &visible, data->box.x, data->box.y, data->box.width, data->box.height);
       pixman_region32_fini(&visible);
     ```
5. `wlr_scene_output_build_state`:
   - :3948-3950: delete the `if (!scene_has_animations(...)) fx_renderer_clear_animation_buffers(output);` block here; re-add it after the render list is built (after `render_data.entry_count = list_len;`), as:
     ```c
       render_data.persistent_visible = false;
       if (scene_has_persistent_effects(scene_output->scene)) {
         for (int i = 0; i < list_len && !render_data.persistent_visible; i++) {
           struct scene_animation* animation = effect_over_node(list_data[i].node);
           render_data.persistent_visible = animation != NULL && animation->persistent;
         }
       }
       if (!scene_has_animations(scene_output->scene) && !render_data.persistent_visible) {
         fx_renderer_clear_animation_buffers(output);
       }
     ```
   - `list_con` initializer (:3995): add `.persistent_effects = scene_has_persistent_effects(scene_output->scene),`. `.calculate_visibility` stays `&& !scene_has_animations(...)`.
   - :4013 whole-output damage: unchanged (transient only).
   - :4249: `if ((fx_pass->has_blur || scene_has_animations(scene_output->scene) || render_data.persistent_visible) && !fx_render_pass_init_offscreen_buffers(render_pass, output))`.
6. Whole-box invalidation for persistent effects. A program may read any texel of its rectangle, and both slot modes only draw damaged pixels into their input (a capture is cleared and then rendered under the damage clip; an in-place copy takes whatever the target holds, which outside the damage is last frame's *output*). So any damage touching a persistent effect's drawn box must grow to the whole box, and the box must reach the commit damage. Add, above `wlr_scene_output_build_state`:
   ```c
   // Adds `box` (buffer coordinates) to the output's damage without scheduling a
   // frame: this runs inside build_state, where a schedule would loop.
   static void scene_output_damage_box_quiet(struct wlr_scene_output* scene_output, const struct wlr_box* box) {
     pixman_region32_t damage;
     pixman_region32_init_rect(&damage, box->x, box->y, box->width, box->height);
     pixman_region32_intersect_rect(&damage, &damage, 0, 0, scene_output->output->width, scene_output->output->height);
     wlr_damage_ring_add(&scene_output->damage_ring, &damage);
     pixman_region32_union(&scene_output->pending_commit_damage, &scene_output->pending_commit_damage, &damage);
     pixman_region32_fini(&damage);
   }

   // The drawn box of a persistent effect, in buffer coordinates: node bounds plus expand, plus the light proxy.
   static bool persistent_effect_box(struct scene_animation* animation, const struct render_data* data, struct wlr_box* box) {
     int lx, ly;
     if (!animation->persistent || !wlr_scene_node_coords(animation->node, &lx, &ly)) {
       return false;
     }
     pixman_region32_t bounds;
     pixman_region32_init(&bounds);
     scene_node_bounds(animation->node, lx, ly, &bounds);
     const pixman_box32_t* extents = pixman_region32_extents(&bounds);
     const int expand = animation_expand(animation);
     *box = (struct wlr_box){
         .x = extents->x1 - data->logical.x - expand,
         .y = extents->y1 - data->logical.y - expand,
         .width = extents->x2 - extents->x1 + 2 * expand,
         .height = extents->y2 - extents->y1 + 2 * expand,
     };
     pixman_region32_fini(&bounds);
     if (box->width <= 0 || box->height <= 0) {
       return false;
     }
     transform_output_box(box, data);
     return true;
   }

   // Adds `box` to `damage` when the region touches it without covering it.
   // Returns true only on real growth.
   static bool damage_grow_to_box(struct wlr_scene_output* scene_output, pixman_region32_t* damage, const struct wlr_box* box, bool commit) {
     pixman_box32_t rect = {.x1 = box->x, .y1 = box->y, .x2 = box->x + box->width, .y2 = box->y + box->height};
     if (pixman_region32_contains_rectangle(damage, &rect) != PIXMAN_REGION_PART) {
       return false; // untouched, or already covered
     }
     pixman_region32_union_rect(damage, damage, box->x, box->y, box->width, box->height);
     if (commit) {
       scene_output_damage_box_quiet(scene_output, box);
     }
     return true;
   }

   // Grows `damage` to cover every persistent effect box it touches, and
   // repeats until nothing grows: covering one box can reach another. Returns
   // true when it grew, so the caller can also widen the commit damage.
   static bool expand_damage_to_effects(struct wlr_scene_output* scene_output, const struct render_data* data, pixman_region32_t* damage, bool commit) {
     struct scene_effects* effects = scene_effects_get(scene_output->scene, false);
     if (effects == NULL || effects->persistent == 0) {
       return false;
     }
     bool grew_any = false;
     bool grew;
     do {
       grew = false;
       struct scene_animation* animation;
       wl_list_for_each(animation, &effects->animations, link) {
         struct wlr_box box;
         if (persistent_effect_box(animation, data, &box)) {
           grew |= damage_grow_to_box(scene_output, damage, &box, commit);
         }
       }
       grew_any |= grew;
     } while (grew);
     return grew_any;
   }
   ```
   Stage 6 adds the output effects to the same loop (a screen effect's box is the whole output, a cursor effect's its square), so one function owns sampling-aware invalidation.
   Call it twice in `build_state`: once on `pending_commit_damage` right after the render list is built and before `wlr_output_state_set_damage(state, &scene_output->pending_commit_damage)` (`commit = true`, so the widened box reaches the ring and the commit), and once on `render_data.damage` right after `wlr_damage_ring_rotate_buffer` fills it (`commit = false`: buffer-age damage from older buffers may touch the box without being part of this commit's damage; rendering more than the committed damage is what wlroots already does with buffer age). Stage 4 extends `persistent_effect_box` with the light proxy's rect (`animation->light->rect` box, when present) so light spill invalidates with its source. Stage 5's `damage-expansion` test covers the mirror case.

- [ ] **Step 5: Add the structural assertion to the test and run**

In `test_persistent_scene`, replace the placeholder `ok &= check(wlr_addon_find(...) == NULL || true, ...)` line with a direct-scanout probe that does not need internals: after removing the slot, render again with a *single* fullscreen rect scene is not scanout-eligible on a headless swapchain, so instead assert through the public surface that the effect state was released:

```c
	// With the slot removed nothing keeps the scene's effect list: the next
	// render must not re-add offscreen buffers (a second render succeeds and the
	// output's fx_offscreen_buffers hold no animation buffers).
	struct wlr_output_state again;
	struct wlr_buffer *plain = fixture_render_scene(fixture, scene_output, &again);
	ok &= check(plain != NULL, "scene renders after the slot is removed");
	struct fx_offscreen_buffers *fbos = fx_offscreen_buffers_try_get(fixture->output);
	ok &= check(fbos == NULL || fbos->animation_buffers[0] == NULL, "animation buffers are released without effects");
	if (plain != NULL) wlr_buffer_unlock(plain);
	wlr_output_state_finish(&again);
```
(`fx_offscreen_buffers_try_get` creates the addon when missing; that is acceptable here — the assertion is on the buffer slot.)

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs'`
Expected: `effects-persistent-scene` passes with all three pixel assertions and the buffer-release assertion; `color-*` and `capture-pacing` pass.

- [ ] **Step 6: Run every animation-related check and the full suite**

Run: `nix develop . --command bash -c 'just build && just check 17 18 19 20 330 60 63 72'`
Expected: all pass. Then `nix develop . --command just check` — expected all pass except known flakes (194, 202, 628, 631); rerun a flake once with `just check-stress <n> 8` to confirm it is the known flake, not a regression.

- [ ] **Step 7: Update `umbrielfx/README.md` and commit**

Add to the README Layout table row for `render/fx_renderer/`: "GLES2 renderer, render passes, effect programs, shaders", and to `tests/`: "Color transform, effect program, scene ABI, and frame pacing regressions". Add a Constraints bullet:

```
- Effect slots come in two classes. Transient (animation) slots keep the
  scene-wide policy: no scanout, no opaque culling, whole-output damage while
  one runs. Persistent (window, overlay, border) slots exempt only their own
  subtree from culling, damage only their drawn bounds, and veto scanout only
  on outputs where they draw. `tests/effects.c` covers the split.
```

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): per-scene effect state with persistent slot isolation"
```

---

### Task 1.5: Stage gate

- [ ] **Step 1: Full verification**

Run:
```bash
cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'just format && git diff --exit-code && just lint && just test && just gpu-test && just check'
```
Expected: `just format` changes nothing (the diff is empty), lint clean, unit and umbrielfx suites green, `umbrielfx-renderer-test` passes on every render node, harness green except known flakes.

- [ ] **Step 2: Squash review**

`git log --oneline 8b06b04..HEAD` shows the four Stage 1 commits. They stay as they are (each builds and passes on its own); Stage 2 continues on top.
