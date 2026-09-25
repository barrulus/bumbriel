# Effects (PR 1) Implementation Plan — Index

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land every effect kind (`animation`, `border`, `window`, `screen`, `cursor`), border light, window overlays, and drag physics in the maintainer's approved named-preset shape, with zero cost when no effect is configured.

**Architecture:** umbrielfx gains one program type (`fx_effect_shader`) with per-kind entry points, generic named uniforms, 13 composition slots split into transient (animation) and persistent (border/window/overlay) slots, an in-place slot mode, per-scene effect state, border light, screen/cursor output addons, and an unfiltered capture composition. The compositor gains `src/config/effects.{h,cpp}` (presets, selectors, post-load validation), a Server-owned `EffectRegistry` (one compiled program per referenced preset, palette uniforms, frame-eligibility ledger), `ViewEffects` on `View` (resolution, gating, slot application shared with overview cards), effect-only frame scheduling in `Output::handleFrame`, and `DragPhysics`.

**Tech Stack:** C23 (umbrielfx, GLES 2 / GLSL ES 1.00, wlroots 0.20 scene fork), C++23 (compositor), meson, toml++, the repository's `check.h` unit harness, the bash harness in `tests/harness`.

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` — every stage plan argues from it; read the spec before any stage.

## Plan set

Each commit stage of the spec is its own plan. Execute them in order; each ends with a green `just test`, `just check`, and a commit.

| Stage | Plan | Commit subject |
| --- | --- | --- |
| 1 | `2026-09-25-effects-01-umbrielfx-programs.md` | `feat(umbrielfx): effect program kinds, generic uniforms, per-scene effect state` |
| 2 | `2026-09-25-effects-02-config-registry.md` | `feat(config): effect presets, selectors, and the effect registry` |
| 3 | `2026-09-25-effects-03-animation-migration.md` | `feat(animation): bind animation presets through effect = ` |
| 4 | `2026-09-25-effects-04-border-light.md` | `feat(effects): border effects with light` |
| 5 | `2026-09-25-effects-05-window.md` | `feat(effects): in-place window effects and capture policy` |
| 6 | `2026-09-25-effects-06-screen-cursor.md` | `feat(effects): screen and cursor effects` |
| 7 | `2026-09-25-effects-07-drag-physics.md` | `feat(animation): drag physics` |
| 8 | `2026-09-25-effects-08-examples-docs-perf.md` | `docs(effects): bundled effects, user and design docs, performance notes` |

## Global Constraints

Copied from the spec; every task's requirements include these.

Execution rules from review:

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe. Repeat this check at every stage gate, including examples, tests, and Markdown.

- Branch `feat/shader-engine`, based on upstream `8346073`. The spec file `docs/superpowers/specs/2026-09-25-effects-design.md` and these plans reference the fork and must be removed from the branch before the pull request is opened (Stage 8, last task).
- Every effect is off by default. A configuration that sets no effect keys renders, performs, and behaves exactly as upstream: no additional compilation, allocations, addons, scene nodes, timers, or clock reads relative to upstream (spec §5, the merge gate).
- `[animation.windows_drag] physics = true`, never "wobble". Nothing in code, config, or docs is called "wobble". Call this deviation out in the PR description.
- `[animation.<event>] shader` is removed (reported as an unknown key). `effect = "<name>"` replaces it and must name an `animation` preset.
- Preset name `off` is reserved and cannot be defined. Selectors take a name or `""`; per-window and per-output overrides take a name or `"off"`.
- Shader files: relative to the declaring TOML, regular file, nonblank, NUL-free, at most 256 KiB, nonblocking open, watched even when missing.
- Duplicate preset names across files are an Error naming both files. A load with any Error keeps the previous configuration.
- `FX_ANIMATION_SLOTS` becomes 13. Descendants compose before ancestors; same-node slots compose in ascending index order.
- Persistent slots (window, overlay, border_effect) never contribute to `scene_has_animations()`. Their damage is confined to their drawn bounds; their scanout veto, culling exceptions, and offscreen buffers apply only on outputs where their drawn result is visible.
- `Server::settled()` and `Output::handleFrame`'s `animationsActive` must stay false for persistent effects; effect-only frames use their own timer, capped by `[effects] max_fps` (0 follows the refresh rate).
- Shared preamble names: `umbriel_sample(uv)`, `umbriel_sample_previous(uv)`, `umbriel_size` (logical), `umbriel_scale`, `umbriel_time`, `umbriel_palette_count`, `umbriel_palette_at(t)`. Entry points: `vec4 animation(vec2 uv)`, `vec4 border(vec2 uv)`, `vec4 window(vec2 uv)`, `vec4 screen(vec2 uv)`, `vec4 cursor(vec2 uv)`. `uv` is normalized over the drawn rectangle, `(0,0)` top left. Results are premultiplied RGBA. No `#version`, `main`, or precision qualifiers in user sources.
- Palette order: `accent_primary`, `accent_secondary`, `warning`, `error` from `[colors]`.
- Harness rules (`CONTRIBUTING.md:93-129`): no wall-clock waits (use `settle`, `clock-freeze`, `clock-advance`), `$UMBRIEL_PIXEL_PROBE` over ImageMagick in new checks, numbering `7xx` rendering / `4xx` drag, `just check-stress <name>` on every new check.
- Conventional Commits (`type(scope): imperative summary`). No `Co-Authored-By` lines and no Claude attribution anywhere (user instruction).
- Toolchain only exists inside `nix develop .`; commits need `GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im` prefixed.
- Verification per stage: `just format`, `just lint`, `just test`, `just check <numbers touched>`, and the full `just check` before the stage commit; `just gpu-test` after Stage 1 and Stage 5.

## Before Stage 1: the working tree

The tree carries uncommitted work that the spec supersedes: `meson.build` (installs `water-*.glsl` and `beach-surf/`), `docs/user/animation.md` (describes them), and untracked `examples/shaders/water-splash.glsl`, `examples/shaders/water-conjure.glsl`, `examples/shaders/beach-surf/`. `beach-surf/preset.toml` uses the fork's `[effects.<name>.<scope>]` grammar, not the spec's `[effects.preset.<name>]`. Preserve rather than delete:

```bash
cd /home/barrulus/dev/umbriel
git stash push -u -m "pre-effects: water and beach-surf shaders" -- meson.build docs/user/animation.md examples/shaders/water-splash.glsl examples/shaders/water-conjure.glsl examples/shaders/beach-surf
git status --short   # expect only: M docs/superpowers/specs/2026-09-25-effects-design.md, ?? docs/superpowers/plans/
```

Commit the spec and plans first: `git add docs/superpowers && git commit -m "docs(spec): effects PR 1 plan set"`.

## File structure (whole PR)

New files, with the one responsibility each carries:

| Path | Responsibility |
| --- | --- |
| `umbrielfx/include/umbrielfx/render/effect.h` | Public program type, kinds, uniforms, slots, node/output attachment API (replaces `animation.h`) |
| `umbrielfx/internal/render/fx_renderer/effect.h` | `struct fx_effect_shader`, uniform location cache, internal draw helpers |
| `umbrielfx/render/fx_renderer/effect_shader.c` | Compile per kind, preamble, uniform cache and binding, refcount, renderer-destroy |
| `umbrielfx/render/fx_renderer/fx_pass.c` (extended) | Capture composite with role-keyed history, in-place pass, light cache/pyramid, unfiltered capture save |
| `umbrielfx/render/fx_renderer/shaders/effect_light.frag` | Emission threshold and screen-blend program |
| `umbrielfx/tests/render_fixture.h` | Headless renderer and scene fixture extracted from `color.c` |
| `umbrielfx/tests/effects.c` | Program kinds, uniforms, expand, persistent isolation, border geometry, light, in-place, capture policy, output effects |
| `src/config/effects.h/.cpp` | `EffectKind`, `EffectPreset`, `Effects`, `readShaderSource`, reference checks |
| `src/scene/effect_ledger.h` | Pure frame-eligibility ledger (per-output eligible counts, active count, suspension) |
| `src/scene/effect_registry.h/.cpp` | Server-owned compiled-program registry, built-in fade and deformation, palette/time uniforms, output effects, pointer |
| `src/view/effects.h`, `src/view/effects_rules.cpp` (pure), `src/view/effects.cpp` (core) | `ViewEffects`: resolve border/window/screen names, gate, apply slots |
| `src/view/drag_physics.h/.cpp` | `DragPhysics` spring sheet |
| `tests/unit/effects.cpp`, `tests/unit/drag_physics.cpp` | Unit tests for the pure parts |
| `tests/harness/clients/toplevel_capture_client.cpp` | ext-image-copy capture of one toplevel, prints its centre colour |
| `tests/harness/checks/{750,751,760,770,771,780,480}_*.sh` | Harness checks (771 carries the spec's capture-feedback extension of 770) |
| `examples/effects/<kind>/<name>/{shader.glsl,effect.toml}` | Bundled presets |
| `docs/user/effects.md`, `docs/design/effects.md` | Documentation |

Removed: `src/config/animation_shader.{h,cpp}` (absorbed by `src/config/effects.{h,cpp}`), `umbrielfx/include/umbrielfx/render/animation.h` (replaced by `effect.h`), `examples/shaders/`.

Modified (main ones): `umbrielfx/types/scene/wlr_scene.c`, `umbrielfx/render/fx_renderer/{fx_pass.c,shaders.c,fx_texture.c,fx_framebuffer.c,fx_offscreen_buffers.c}`, `umbrielfx/internal/render/fx_renderer/{shaders.h,fx_renderer.h,animation_history.h}`, `umbrielfx/meson.build`, `src/config/{config.h,config.cpp,change.h,change.cpp,config_merge.h,config_merge.cpp,resolve.cpp}`, `src/scene/animation_shader.{h,cpp}`, `src/view/{view.h,view.cpp,decoration.h,decoration.cpp,border_ring.h,border_ring.cpp}`, `src/scene/border_rect.h`, `src/server/{server.h,server.cpp,server_events.cpp,ipc_commands.h,ipc_commands.cpp}`, `src/output/{output.h,output.cpp}`, `src/input/cursor.{h,cpp}`, `src/overview/overview.{h,cpp}`, `meson.build`, `tests/meson.build`, `tests/unit/config_load.cpp`, 24 harness checks using `shader =`, docs.

## Shared interfaces (the glossary every stage uses)

These names are fixed here so that later stages match earlier ones. A stage that needs to change one must update this table and the other stages.

### umbrielfx (`include/umbrielfx/render/effect.h`)

```c
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

enum fx_effect_kind { FX_EFFECT_ANIMATION, FX_EFFECT_BORDER, FX_EFFECT_WINDOW, FX_EFFECT_SCREEN, FX_EFFECT_CURSOR };
enum fx_uniform_type { FX_UNIFORM_FLOAT, FX_UNIFORM_VEC2, FX_UNIFORM_VEC3, FX_UNIFORM_VEC4, FX_UNIFORM_INT, FX_UNIFORM_BOOL };
#define FX_UNIFORM_NAME_MAX 32
#define FX_UNIFORM_FLOATS_MAX 32
#define FX_UNIFORMS_MAX 8
struct fx_uniform {
  char name[FX_UNIFORM_NAME_MAX];
  enum fx_uniform_type type;
  unsigned count;                        // array elements; 1 for a scalar or vector
  float floats[FX_UNIFORM_FLOATS_MAX];   // FLOAT/VEC*: count * components values
  int32_t ints[4];                       // INT/BOOL: count values, count <= 4
};
struct fx_effect_light { bool enabled; float spread; float intensity; float threshold; };
struct fx_animation_parameters {
  float progress; float linear_progress; float direction;
  uint64_t transition_id; float random_seed[4];
  int expand;                            // logical px; honoured by FX_SLOT_BORDER_EFFECT and FX_SLOT_DRAG
  unsigned uniform_count;
  struct fx_uniform uniforms[FX_UNIFORMS_MAX];
  struct fx_effect_light light;          // FX_SLOT_BORDER_EFFECT only
};
struct fx_uniform* fx_parameters_add_uniform(struct fx_animation_parameters*, const char* name, enum fx_uniform_type, unsigned count);
struct fx_effect_shader* fx_effect_shader_create(struct wlr_renderer*, enum fx_effect_kind, const char* source, const char* label);
struct fx_effect_shader* fx_effect_shader_ref(struct fx_effect_shader*);
void fx_effect_shader_unref(struct fx_effect_shader*);
void fx_effect_shader_set_shape_preserving(struct fx_effect_shader*, bool);
bool fx_effect_shader_reads(const struct fx_effect_shader*, const char* uniform);
enum fx_effect_kind fx_effect_shader_kind(const struct fx_effect_shader*);
/* unchanged signatures: */
void wlr_scene_node_set_animation(struct wlr_scene_node*, unsigned slot, struct fx_effect_shader*, const struct fx_animation_parameters*);
void wlr_scene_node_clear_animations(struct wlr_scene_node*);
bool wlr_scene_node_set_animation_output_clip(struct wlr_scene_node*, const struct wlr_box*);
void wlr_scene_node_copy_animations_for_snapshot(struct wlr_scene_node* destination, struct wlr_scene_node* source);
void wlr_scene_shadow_set_animation_source(struct wlr_scene_shadow*, struct wlr_scene_node* source, const float color[4]);
/* stage 4 */ void wlr_scene_set_effect_light_layer(struct wlr_scene*, struct wlr_scene_tree* layer);
/* stage 5 */ void wlr_scene_output_set_effect_capture_policy(struct wlr_scene_output*, bool in_capture);
/* stage 6 */ void wlr_scene_output_set_screen_effect(struct wlr_scene_output*, struct fx_effect_shader*, const struct fx_animation_parameters*);
/* stage 6 */ void wlr_scene_output_set_cursor_effect(struct wlr_scene_output*, struct fx_effect_shader*, const struct fx_animation_parameters*, int radius);
/* stage 6 */ void wlr_scene_output_set_effect_pointer(struct wlr_scene_output*, double lx, double ly, bool visible);
```

Uniforms pushed by the compositor through `uniforms[]`: `umbriel_time` (FLOAT), `umbriel_palette` (VEC4, count 4), `umbriel_palette_count` (INT), `umbriel_deformation` (VEC2, count 16). Uniforms umbrielfx appends itself at draw time through the same binder: `umbriel_size`, `umbriel_scale`, `umbriel_expand` (vec2, padding as a fraction of the drawn rectangle per axis), `umbriel_border_hole` (vec4, hole in uv of the drawn rectangle), `umbriel_border_radius` (vec4 tl,tr,br,bl, logical px), `umbriel_corner_radius` (vec4, window mask), `umbriel_pointer` (vec2 uv).

### Compositor

```cpp
// src/config/effects.h
enum class EffectKind : std::uint8_t { Animation, Border, Window, Screen, Cursor };
struct ShaderSource { std::string code; std::filesystem::path file; bool operator==(const ShaderSource&) const = default; };
struct ShaderReadResult { std::optional<ShaderSource> source; std::vector<std::filesystem::path> watchPaths; };
inline constexpr std::size_t kShaderSourceLimit = 256 * 1024;
inline constexpr std::string_view kEffectOff = "off";
[[nodiscard]] ShaderReadResult readShaderSource(Section& section, std::vector<ConfigDiagnostic>& diagnostics);
struct BorderLight { int spread = 80; float intensity = 1.0F; float threshold = 0.5F; bool operator==(const BorderLight&) const = default; };
struct EffectPreset { std::string name; EffectKind kind = EffectKind::Animation; ShaderSource shader; bool palette = false; int padding = 0; float speed = 1.0F; bool animated = true; std::string overlay; std::optional<BorderLight> light; int radius = 0; bool operator==(const EffectPreset&) const = default; };
struct Effects { std::vector<EffectPreset> presets; std::string border, window, screen, cursor; int maxFps = 0; bool inCapture = false; bool operator==(const Effects&) const = default; };
[[nodiscard]] std::optional<EffectKind> parseEffectKind(std::string_view text);
[[nodiscard]] std::string_view effectKindName(EffectKind kind);
[[nodiscard]] const EffectPreset* findEffectPreset(const Effects& effects, std::string_view name);
// "" and (when allowOff) "off" are valid; otherwise the preset must exist with `kind`. Returns the diagnostic text on failure.
[[nodiscard]] std::optional<std::string> effectReferenceError(const Effects& effects, std::string_view name, EffectKind kind, bool allowOff);
// src/config/config.h additions
Config::Effects → `Effects effects;`  (the struct above, at namespace scope)
Config::Animation::<event>::effect (std::string, replaces shader)
Config::Animation::WindowsDrag { bool physics = false; } windowsDrag;
WindowRule::borderEffect, WindowRule::windowEffect, ResolvedWindowRule::borderEffect, ResolvedWindowRule::windowEffect : std::optional<std::string>
OutputRule::screenEffect : std::optional<std::string>
[[nodiscard]] std::array<std::array<float, 4>, 4> effectPalette(const Config::Colors& colors);
ConfigChange::effects, ConfigEffects::effects (bool)

// src/scene/animation_shader.h — slot enum, same values as FX_SLOT_*
enum class AnimationEvent : unsigned { Window, Overlay, BorderEffect, Border, DimUnfocused, WindowsMove, Drag, WindowsIn, WindowsOut, Scratchpad, Layers, Workspaces, Overview };

// src/scene/effect_ledger.h (pure)
struct EffectInstanceState { const void* output = nullptr; bool visible = false; bool readsTime = false; bool advancing = false; };
class EffectLedger { void update(const void* owner, const EffectInstanceState&); void remove(const void* owner); void removeOutput(const void* output); void setSuspended(bool); [[nodiscard]] bool suspended() const; [[nodiscard]] unsigned eligible(const void* output) const; [[nodiscard]] unsigned active() const; };

// src/scene/effect_registry.h (core)
class EffectRegistry {
  explicit EffectRegistry(Server& server);
  void prepare(wlr_renderer* renderer);            // startup, reload with effects flag, renderer recovery
  void clear();                                     // before wlr_renderer_destroy
  [[nodiscard]] wlr_renderer* renderer() const;
  [[nodiscard]] fx_effect_shader* preset(std::string_view name, EffectKind kind) const;
  [[nodiscard]] const EffectPreset* presetConfig(std::string_view name) const;
  [[nodiscard]] fx_effect_shader* animationShader(AnimationEvent event) const;
  [[nodiscard]] fx_effect_shader* lifecycleShader(AnimationEvent event) const;
  [[nodiscard]] fx_effect_shader* deformationShader();      // stage 7, lazy
  void fillTimeUniforms(fx_animation_parameters& params, float seconds, const EffectPreset& preset, const fx_effect_shader* shader) const; // time only when the program reads it; palette
  [[nodiscard]] bool active() const;                        // any view or output carries a persistent effect
  [[nodiscard]] EffectLedger& ledger();
  void updateInstance(const void* owner, const EffectInstanceState& state);   // stage 4; pokes the output when it becomes eligible
  void removeInstance(const void* owner);                   // stage 4
  void removeOutput(const Output* output);                  // stage 4
  void ensureLightLayer();                                  // stage 4; creates the layer when a referenced border preset has light
  void setSuspended(bool suspended);                        // session lock
  void applyOutputEffects();                                // stage 5 scaffold (capture policy), stage 6 (screen, cursor)
  void pointerMoved(double lx, double ly, bool visible);    // stage 6
  [[nodiscard]] bool cursorEffectActive() const;            // stage 6
  [[nodiscard]] fx_effect_shader* deformationShader();      // stage 7, lazy
  [[nodiscard]] const EffectPreset* animationPreset(AnimationEvent event) const;   // stage 3
  [[nodiscard]] float clockSeconds() const;                 // stage 3; animation clock, read only when a program needs it
};
[[nodiscard]] EffectRegistry& effectRegistry();   // the Server's registry: an embedded member `EffectRegistry m_effects` (no heap allocation)
Overview::syncCardEffects(Card&) / syncCardEffects()   // stage 4; card slots refresh every tick, not only on relayout
Server::ensureEffectLightLayer() → wlr_scene_tree* (stage 4)

// src/view/effects.h (pure parts in effects_rules.cpp, the class in effects.cpp)
struct ViewEffectNames { std::string border; std::string window; };
[[nodiscard]] ViewEffectNames resolveViewEffectNames(const Effects& effects, const ResolvedWindowRule& rule);   // pure; "" = off
[[nodiscard]] std::string resolveScreenEffectName(const Effects& effects, const OutputRule* rule);            // pure; stage 6
struct BorderEffectGate { bool focused; bool decorated; bool urgent; bool fullscreen; };
[[nodiscard]] bool borderEffectApplies(const BorderEffectGate& gate);   // pure
class ViewEffects {
  void resolve(const Effects& effects, const ResolvedWindowRule& rule);
  [[nodiscard]] bool configured() const;   // a border or window preset is selected; the clock is read only then
  [[nodiscard]] int borderPadding() const;
  struct ApplyInput { wlr_scene_node* surface; wlr_scene_node* captureSurface /* stage 5 */; wlr_scene_node* border; BorderEffectGate gate; float seconds; bool clockAdvancing; const void* output; wlr_box outputBox; };
  void apply(const ApplyInput& input);     // ledger instances keyed by node (border, surface, &surface->addons for the overlay); visibility = node->visible ∩ outputBox
  void detach();                           // every instance this object registered
  void detachNodes(wlr_scene_node* surface, wlr_scene_node* border);   // one card's nodes
};
View::syncAnimationShaders(wlr_scene_tree* target = nullptr, wlr_scene_node* border = nullptr, wlr_scene_node* surface = nullptr, const BorderEffectGate* gate = nullptr, Output* cardOutput = nullptr);
View::effects() → ViewEffects&;  View::borderEffectPadding() → int

// src/view/drag_physics.h (pure)
class DragPhysics { void begin(float width, float height, float grabX, float grabY); void move(float dx, float dy); void release(); bool tick(double seconds); [[nodiscard]] bool active() const; [[nodiscard]] std::array<std::array<float,2>,16> normalizedDisplacement() const; [[nodiscard]] float maxDisplacement() const; ... };
```

## Verification commands (every stage)

```bash
cd /home/barrulus/dev/umbriel && nix develop . --command bash -c '
just format && just lint && just test && just check <touched check numbers>'
# before the stage commit:
nix develop . --command just check
```

## Execution notes

- Subagent-driven execution: give each subagent the stage plan, this index, and the spec. The subagent implements one task, runs that task's verification, and reports.
- Never commit with attribution trailers. Commit message bodies describe what changed and why in imperative prose.
- When a step says "Run:" and "Expected:", the expected output is what passes; anything else is a stop-and-investigate.
