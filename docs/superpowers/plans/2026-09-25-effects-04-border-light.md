# Effects Stage 4: Border effects and light

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `border` preset selected by `[effects] border`, `[[window_rule]] border_effect`, or `"off"` renders on the focused, decorated, non-urgent, non-fullscreen window's border ring (with `padding`), optionally emits light through a blurred pyramid drawn in a dedicated scene layer, drives effect-only frames capped by `max_fps`, follows focus, survives close snapshots with frozen time, and shows on overview cards.

**Architecture:** umbrielfx learns the border geometry of the node it composites (hole and radii → `umbriel_border_hole`/`umbriel_border_radius`), keeps an optional light cache per border slot (emission texture → thresholded half-res level → Kawase pyramid → screen blend), and draws the light from an input-transparent proxy rect in the light layer that `src` registers. In the compositor, `ViewEffects` resolves names and gates the border slot; `EffectRegistry` records instances in the ledger and pokes outputs when eligibility appears; `Output` owns a lazy timer for effect-only frames.

**Tech Stack:** C23 umbrielfx, C++23, harness with `$UMBRIEL_PIXEL_PROBE`, a harness-only IPC command for frame counts.

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §2 (`ViewEffects`, Policy: Time, Frames), §3 (border kind, Slots, Border light), §4 (border), §6 (750, 751, 780). Index: `2026-09-25-effects-00-index.md`.

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Reuse `makeBorderRing`, `toplevelSurfaceTreeNode`, scene visibility/bounds/clip helpers, renderer projection/copy/blur helpers, and the existing animation clock and output scheduling policy. Expose a narrow umbrielfx query around its existing visibility traversal instead of duplicating the scene walk in C++. Share effect attachment/time updates between live views and overview cards.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- Border gating: focused, decorated, not urgent, not fullscreen (`View::m_borderFocusedState`, `decorated()`, `m_urgent`, `m_toplevel->scheduled.fullscreen`).
- Time: animation clock seconds × `speed`; 0 when `animated = false` or `speed = 0`. A frozen clock or a snapshot never advances.
- Effect-only frames come from `Output`'s own timer, never from `Server::animationsActive()`; `settle` keeps working with a border effect running.
- Light: emission from the border slot's result, suppressed under a transient ancestor, never from snapshots, never for borders stacked above the light layer.
- Light layer: created lazily by `src` above `m_dragIconTree`, below the TOP shell layer, registered with `wlr_scene_set_effect_light_layer`.

---

### Task 4.1: Border geometry uniforms and `fx_render_pass_end_effect`

**Files:**
- Modify: `umbrielfx/internal/render/fx_renderer/effect.h`, `umbrielfx/internal/render/fx_renderer/animation_history.h`, `umbrielfx/render/fx_renderer/fx_pass.c` (`draw_animation_texture`, `fx_render_pass_end_animation_with_history`, `fx_render_pass_end_animation`), `umbrielfx/types/scene/wlr_scene.c` (`render_animated_range`)
- Test: `umbrielfx/tests/effects.c` (`border-geometry` case)

**Interfaces:**
- Produces (internal `effect.h`):
  ```c
  // Node-local border geometry, relative to the drawn logical box's origin.
  struct fx_effect_geometry { struct wlr_box hole; float radius[4]; /* tl, tr, br, bl logical px */ };
  struct fx_effect_light_cache;   // Task 4.2
  struct fx_effect_composite {
    struct fx_effect_shader* shader;
    const struct fx_animation_parameters* parameters;
    struct wlr_box box;          // node box, buffer px
    struct wlr_box logical_box;  // node box, logical
    enum wl_output_transform transform;
    int expand;
    const pixman_region32_t* capture_clip;
    const pixman_region32_t* output_clip;
    struct fx_animation_history* history;
    struct wlr_output* output;
    bool update_history;
    const struct fx_effect_geometry* geometry;  // NULL unless a border slot composites a border node
    struct fx_effect_light_cache* light;        // NULL unless this composite emits light
  };
  void fx_render_pass_end_effect(struct fx_gles_render_pass* pass, const struct fx_effect_composite* composite);
  ```
- `fx_render_pass_end_animation_with_history` is removed; `render_animated_range` builds a `fx_effect_composite`. `fx_render_pass_end_animation` (public, no history) stays as a wrapper.

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
// A border program sees the client hole through umbriel_border_hole and
// umbriel_border_distance, and its result is cut out of the hole.
static bool test_border_geometry(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, black);
	// 12x12 ring at (2,2) with a 2px wall: the hole is 8x8 at (2,2) node-local.
	struct wlr_scene_border *border = wlr_scene_border_create(&scene->tree, white, white);
	wlr_scene_border_set_geometry(border, 12, 12, 2, 0,
		(struct clipped_region){ .area = { 2, 2, 8, 8 } }, (struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 2, 2);
	struct fx_effect_shader *program = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv) { return vec4(0.0, 0.0, 1.0, 1.0) * step(0.0, umbriel_border_distance(uv)); }", "border-geometry");
	bool ok = check(program != NULL, "border program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, program, &parameters);
	struct wlr_output_state state;
	struct wlr_buffer *rendered = fixture_render_scene(fixture, scene_output, &state);
	ok &= check(rendered != NULL, "renders");
	if (rendered != NULL) {
		uint8_t ring[4], hole[4], outside[4];
		ok &= fixture_read_pixel(fixture, rendered, 3, 8, ring);      // inside the 2px wall
		ok &= fixture_read_pixel(fixture, rendered, 8, 8, hole);      // hole centre
		ok &= fixture_read_pixel(fixture, rendered, 0, 0, outside);   // past the node
		ok &= check(ring[0] > 250 && ring[2] < 5, "the ring is blue where the distance is positive");
		ok &= check(hole[0] < 5 && hole[1] < 5 && hole[2] < 5, "the hole is cut out of the result");
		ok &= check(outside[0] < 5, "nothing draws past the border box");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	fx_effect_shader_unref(program);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```
Register `border-geometry` in `main` and the meson case list.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-border-geometry --print-errorlogs'`
Expected: FAIL on "the ring is blue" (the hole and radius uniforms are zero, so `umbriel_border_distance` is meaningless and the suffix cuts everything).

- [ ] **Step 3: Implement**

`umbrielfx/internal/render/fx_renderer/effect.h`: add the two structs from the interface block and the `fx_render_pass_end_effect` declaration (include `<pixman.h>`, `<wayland-server-protocol.h>`, `<wlr/util/box.h>`; forward-declare `struct fx_gles_render_pass`, `struct fx_animation_history`, `struct wlr_output`).

`umbrielfx/render/fx_renderer/fx_pass.c`:

1. `draw_animation_texture` gains `const struct fx_effect_geometry* geometry` after `expand`. After the `umbriel_expand` upload add:
```c
  if (geometry != NULL) {
    struct fx_uniform hole = {.name = "umbriel_border_hole", .type = FX_UNIFORM_VEC4, .count = 1};
    hole.floats[0] = logical_box->width > 0 ? (float)geometry->hole.x / logical_box->width : 0;
    hole.floats[1] = logical_box->height > 0 ? (float)geometry->hole.y / logical_box->height : 0;
    hole.floats[2] = logical_box->width > 0 ? (float)geometry->hole.width / logical_box->width : 0;
    hole.floats[3] = logical_box->height > 0 ? (float)geometry->hole.height / logical_box->height : 0;
    fx_effect_shader_bind_uniform(shader, &hole);
    struct fx_uniform radius = {.name = "umbriel_border_radius", .type = FX_UNIFORM_VEC4, .count = 1};
    memcpy(radius.floats, geometry->radius, sizeof(geometry->radius));
    fx_effect_shader_bind_uniform(shader, &radius);
  }
```
(`logical_box` is the drawn box at this point. The caller expresses `hole` relative to the drawn box origin, see step 4.)
2. Replace `fx_render_pass_end_animation_with_history` with `fx_render_pass_end_effect(pass, composite)`: same body, reading fields from `composite`, with `expand_animation_boxes` applied to local copies of `composite->box`/`logical_box`, passing `composite->geometry` to both draws; the hole in `geometry` is shifted by `expand` before drawing (`hole.x += expand; hole.y += expand` on a local copy) because the drawn box grew. Keep the light hook for Task 4.2: immediately before the `wlr_texture_destroy(texture);` that ends both the history path and the fallback path, add `if (composite->light != NULL) { emit_light(pass, composite, texture, previous_texture, ...); }` as a `static` stub that Task 4.2 fills (for now it does nothing).
3. `fx_render_pass_end_animation` becomes:
```c
void fx_render_pass_end_animation(struct fx_gles_render_pass* pass, struct fx_effect_shader* shader,
    const struct fx_animation_parameters* parameters, const struct wlr_box* box, const struct wlr_box* logical_box,
    enum wl_output_transform transform, const pixman_region32_t* clip, int expand) {
  const struct fx_effect_composite composite = {
      .shader = shader, .parameters = parameters, .box = *box, .logical_box = *logical_box, .transform = transform,
      .expand = expand, .capture_clip = clip, .output_clip = clip,
  };
  fx_render_pass_end_effect(pass, &composite);
}
```
4. Remove the declaration from `animation_history.h` (keep the history API).

`umbrielfx/types/scene/wlr_scene.c`, `render_animated_range`:

Add a helper above it:
```c
// The border node a border slot composites: the node itself, or the single
// enabled border child of a tree (a view's border tree). `hole` comes back in
// logical coordinates relative to `origin` (the animated node's bounds origin).
static bool border_geometry(
    struct wlr_scene_node* node, int lx, int ly, const pixman_box32_t* extents, struct fx_effect_geometry* geometry
) {
  struct wlr_scene_border* border = NULL;
  int bx = lx, by = ly;
  if (node->type == WLR_SCENE_NODE_BORDER) {
    border = wlr_scene_border_from_node(node);
  } else if (node->type == WLR_SCENE_NODE_TREE) {
    struct wlr_scene_node* child;
    wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
      if (child->enabled && child->type == WLR_SCENE_NODE_BORDER) {
        if (border != NULL) {
          return false;
        }
        border = wlr_scene_border_from_node(child);
        bx = lx + child->x;
        by = ly + child->y;
      }
    }
  }
  if (border == NULL) {
    return false;
  }
  geometry->hole = border->clipped_region.area;
  geometry->hole.x += bx - extents->x1;
  geometry->hole.y += by - extents->y1;
  geometry->radius[0] = border->clipped_region.corners.top_left;
  geometry->radius[1] = border->clipped_region.corners.top_right;
  geometry->radius[2] = border->clipped_region.corners.bottom_right;
  geometry->radius[3] = border->clipped_region.corners.bottom_left;
  return true;
}
```
In the composite loop (`for (unsigned slot = 0; slot < FX_ANIMATION_SLOTS; slot++) if (captured[slot])`), replace the call with:
```c
        struct fx_effect_geometry geometry;
        const bool has_geometry = slot == FX_SLOT_BORDER_EFFECT
            && fx_effect_shader_kind(animation->shaders[slot]) == FX_EFFECT_BORDER
            && border_geometry(animation->node, lx, ly, extents, &geometry);
        const struct fx_effect_composite composite = {
            .shader = animation->shaders[slot],
            .parameters = &animation->parameters[slot],
            .box = box,
            .logical_box = logical_box,
            .transform = data->transform,
            .expand = expand,
            .capture_clip = &clip,
            .output_clip = composite_clip,
            .history = &animation->histories[slot],
            .output = data->output->output,
            .update_history = !data->shadow_capture,
            .geometry = has_geometry ? &geometry : NULL,
            .light = NULL,   // Task 4.2
        };
        fx_render_pass_end_effect(pass, &composite);
```
(`extents` is the `pixman_region32_extents(&bounds)` pointer; keep it alive until here by moving `pixman_region32_fini(&bounds)` after the loop.)

- [ ] **Step 4: Run tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs && just build && just check 18 19 20'`
Expected: `effects-border-geometry` and every other case pass; animation checks pass.

- [ ] **Step 5: Commit**

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): border geometry uniforms for border slots"
```

---

### Task 4.2: Border light

**Files:**
- Create: `umbrielfx/render/fx_renderer/shaders/effect_light.frag`
- Modify: `umbrielfx/meson.build:138-153` (shader list), `umbrielfx/render/fx_renderer/shaders.c` (include the embedded source), `umbrielfx/internal/render/fx_renderer/effect.h`, `umbrielfx/internal/render/fx_renderer/fx_renderer.h` (`struct fx_renderer`: `GLuint effect_light_program; GLint effect_light_{proj,tex_proj,pos,tex,gain,linear,emission,source_linear,threshold,source_region};`), `umbrielfx/render/fx_renderer/fx_renderer.c` (delete the program on destroy), `umbrielfx/render/fx_renderer/fx_pass.c` (light cache, emission, blur, blend), `umbrielfx/types/scene/wlr_scene.c` (light layer, proxy, sync, render), `umbrielfx/include/umbrielfx/render/effect.h` (`wlr_scene_set_effect_light_layer`)
- Test: `umbrielfx/tests/effects.c` (`border-light` case)

**Interfaces:**
- Produces (public): `void wlr_scene_set_effect_light_layer(struct wlr_scene* scene, struct wlr_scene_tree* layer);`
- Produces (internal `effect.h`):
  ```c
  #define FX_LIGHT_LEVELS 6
  struct fx_effect_light_cache {
    struct fx_renderer* renderer; struct wl_listener renderer_destroy;
    GLuint emission_texture, emission_framebuffer; int emission_width, emission_height;
    GLuint textures[FX_LIGHT_LEVELS + 1], framebuffers[FX_LIGHT_LEVELS + 1];
    int widths[FX_LIGHT_LEVELS + 1], heights[FX_LIGHT_LEVELS + 1], levels;
    int margin;            // buffer px around the emission
    bool valid, failed;
  };
  struct fx_effect_light_cache* fx_effect_light_cache_create(struct fx_renderer* renderer);
  void fx_effect_light_cache_destroy(struct fx_effect_light_cache* cache);
  // Screen-blends the blurred emission over `box` (the proxy's buffer box), clipped.
  void fx_render_pass_add_effect_light(struct fx_gles_render_pass* pass, struct fx_effect_light_cache* cache,
      const struct fx_effect_light* light, const struct wlr_box* box, const pixman_region32_t* clip);
  ```
- Produces (static in `wlr_scene.c`): `struct scene_light { struct wlr_addon addon; struct wlr_scene_rect* rect; struct scene_animation* source; struct fx_effect_light_cache* cache; int margin; }`; `scene_effects` gains `struct wlr_scene_tree* light_layer; struct wl_listener light_layer_destroy;`; `scene_animation` gains `struct scene_light* light;`.
- Consumes: `renderer->shaders.blur1/blur2` (`struct blur_shader` with `program, proj, tex_proj, pos, tex, radius, halfpixel, sample_bounds` — confirm names in `internal/render/fx_renderer/shaders.h`), `render`, `set_proj_matrix`, `make_tex_matrix`, `set_tex_matrix`, `matrix_projection`.

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
// Light from a border slot spills past the border box into the light layer,
// only when the scene has one.
static bool render_light_scene(struct fixture *fixture, bool with_layer, uint8_t margin_pixel[4], uint8_t ring_pixel[4]) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, black);
	struct wlr_scene_border *border = wlr_scene_border_create(&scene->tree, white, white);
	wlr_scene_border_set_geometry(border, 8, 8, 2, 0,
		(struct clipped_region){ .area = { 2, 2, 4, 4 } }, (struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 4, 4);
	if (with_layer) {
		wlr_scene_set_effect_light_layer(scene, wlr_scene_tree_create(&scene->tree));
	}
	struct fx_effect_shader *program = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv) { return vec4(1.0, 0.0, 0.0, 1.0); }", "border-light");
	bool ok = check(program != NULL, "border program compiles");
	struct fx_animation_parameters parameters = {
		.progress = 1, .linear_progress = 1, .direction = 1,
		.light = { .enabled = true, .spread = 3, .intensity = 4, .threshold = 0.1f },
	};
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, program, &parameters);
	struct wlr_output_state state;
	struct wlr_buffer *rendered = fixture_render_scene(fixture, scene_output, &state);
	ok &= check(rendered != NULL, "renders");
	if (rendered != NULL) {
		ok &= fixture_read_pixel(fixture, rendered, 2, 8, margin_pixel);   // 2px left of the border box
		ok &= fixture_read_pixel(fixture, rendered, 5, 8, ring_pixel);     // inside the wall
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	fx_effect_shader_unref(program);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static bool test_border_light(struct fixture *fixture) {
	uint8_t margin[4], ring[4], dark_margin[4], dark_ring[4];
	bool ok = render_light_scene(fixture, true, margin, ring);
	ok &= render_light_scene(fixture, false, dark_margin, dark_ring);
	ok &= check(ring[2] > 250 && dark_ring[2] > 250, "the ring itself is red with and without light");
	ok &= check(margin[2] > 20, "light spills red past the border box");
	ok &= check(margin[2] < ring[2], "the spill is dimmer than the ring");
	ok &= check(dark_margin[2] < 5 && dark_margin[1] < 5, "without a light layer nothing spills");
	return ok;
}
```
Register `border-light`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-border-light --print-errorlogs'`
Expected: compile error (`wlr_scene_set_effect_light_layer` undeclared).

- [ ] **Step 3: The light program**

Create `umbrielfx/render/fx_renderer/shaders/effect_light.frag` (emission mode thresholds the border result into level 0; blend mode maps the blurred level to a screen-blend contribution):

```glsl
precision highp float;
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float gain;
uniform bool linear;
uniform bool emission;
uniform bool source_linear;
uniform float threshold;
uniform vec4 source_region;
void main() {
    if (emission) {
        vec2 uv = (v_texcoord - source_region.xy) / source_region.zw;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            gl_FragColor = vec4(0.0);
            return;
        }
        vec4 value = texture2D(tex, uv);
        if (source_linear && value.a > 0.0) {
            vec3 rgb = value.rgb / value.a;
            value.rgb = mix(rgb * 12.92, 1.055 * pow(max(rgb, 0.0), vec3(1.0 / 2.4)) - 0.055,
                step(vec3(0.0031308), rgb)) * value.a;
        }
        float peak = max(value.r, max(value.g, value.b));
        gl_FragColor = value * smoothstep(threshold, max(threshold + 0.001, 1.0), peak);
        return;
    }
    vec3 light = 1.0 - exp(-max(texture2D(tex, v_texcoord).rgb, 0.0) * gain);
    if (linear) {
        light = mix(light / 12.92, pow((light + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), light));
    }
    gl_FragColor = vec4(light, max(light.r, max(light.g, light.b)));
}
```

Add `'effect_light.frag',` to the `foreach shader` list in `umbrielfx/meson.build` and `#include "effect_light_frag_src.h"` to `shaders.c`. Link it lazily on first use (like `animation_shadow_*`): in `fx_pass.c` add
```c
static bool ensure_light_program(struct fx_renderer* renderer) {
  if (renderer->effect_light_program != 0) {
    return true;
  }
  renderer->effect_light_program = link_program(effect_light_frag_src);
  if (renderer->effect_light_program == 0) {
    return false;
  }
  GLuint p = renderer->effect_light_program;
  renderer->effect_light_proj = glGetUniformLocation(p, "proj");
  renderer->effect_light_tex_proj = glGetUniformLocation(p, "tex_proj");
  renderer->effect_light_pos = glGetAttribLocation(p, "pos");
  renderer->effect_light_tex = glGetUniformLocation(p, "tex");
  renderer->effect_light_gain = glGetUniformLocation(p, "gain");
  renderer->effect_light_linear = glGetUniformLocation(p, "linear");
  renderer->effect_light_emission = glGetUniformLocation(p, "emission");
  renderer->effect_light_source_linear = glGetUniformLocation(p, "source_linear");
  renderer->effect_light_threshold = glGetUniformLocation(p, "threshold");
  renderer->effect_light_source_region = glGetUniformLocation(p, "source_region");
  return true;
}
```
(`effect_light_frag_src` must be visible in `fx_pass.c`: include the generated header there too, or expose `const char* fx_effect_light_source(void)` from `shaders.c`.) `fx_renderer_destroy` deletes `effect_light_program` next to the other `glDeleteProgram` calls.

- [ ] **Step 4: Light cache, emission, pyramid, blend (`fx_pass.c`)**

```c
static bool light_target_init(GLuint* texture, GLuint* framebuffer, int width, int height, GLenum type) {
  glGenTextures(1, texture);
  glBindTexture(GL_TEXTURE_2D, *texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, type, NULL);
  glGenFramebuffers(1, framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
  return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void light_cache_release(struct fx_effect_light_cache* cache) {
  glDeleteFramebuffers(1, &cache->emission_framebuffer);
  glDeleteTextures(1, &cache->emission_texture);
  glDeleteFramebuffers(FX_LIGHT_LEVELS + 1, cache->framebuffers);
  glDeleteTextures(FX_LIGHT_LEVELS + 1, cache->textures);
  memset(cache->framebuffers, 0, sizeof(cache->framebuffers));
  memset(cache->textures, 0, sizeof(cache->textures));
  cache->emission_framebuffer = cache->emission_texture = 0;
  cache->emission_width = cache->emission_height = 0;
  cache->valid = false;
  cache->failed = false;
}

static void light_cache_renderer_destroy(struct wl_listener* listener, void* data) {
  struct fx_effect_light_cache* cache = wl_container_of(listener, cache, renderer_destroy);
  // The context releases the GL objects; never touch their names again.
  memset(cache->framebuffers, 0, sizeof(cache->framebuffers));
  memset(cache->textures, 0, sizeof(cache->textures));
  cache->emission_framebuffer = cache->emission_texture = 0;
  cache->valid = false;
  cache->renderer = NULL;
  wl_list_remove(&cache->renderer_destroy.link);
}

struct fx_effect_light_cache* fx_effect_light_cache_create(struct fx_renderer* renderer) {
  struct fx_effect_light_cache* cache = calloc(1, sizeof(*cache));
  if (cache == NULL) {
    return NULL;
  }
  cache->renderer = renderer;
  cache->renderer_destroy.notify = light_cache_renderer_destroy;
  wl_signal_add(&renderer->wlr_renderer.events.destroy, &cache->renderer_destroy);
  return cache;
}

void fx_effect_light_cache_destroy(struct fx_effect_light_cache* cache) {
  if (cache == NULL) {
    return;
  }
  if (cache->renderer != NULL) {
    struct wlr_egl_context previous;
    if (wlr_egl_make_current(cache->renderer->egl, &previous)) {
      light_cache_release(cache);
      wlr_egl_restore_context(&previous);
    }
    wl_list_remove(&cache->renderer_destroy.link);
  }
  free(cache);
}

// Allocates (or reuses) the emission texture and pyramid for a drawn box of
// `width` x `height` buffer pixels with `margin` around it. Half float when the
// renderer can filter it, so screen-blended light does not band.
static bool light_cache_prepare(struct fx_effect_light_cache* cache, int width, int height, int margin, float spread_px) {
  const int full_width = width + 2 * margin;
  const int full_height = height + 2 * margin;
  const float radius = spread_px * 0.5f;
  int levels = (int)ceilf(log2f(radius / 3 + 1));
  levels = levels < 1 ? 1 : (levels > FX_LIGHT_LEVELS ? FX_LIGHT_LEVELS : levels);
  if (cache->textures[0] != 0 && cache->emission_width == width && cache->emission_height == height
      && cache->margin == margin && cache->levels == levels) {
    return !cache->failed;
  }
  light_cache_release(cache);
  cache->emission_width = width;
  cache->emission_height = height;
  cache->margin = margin;
  cache->levels = levels;
  cache->failed = true;
  const GLenum type = cache->renderer->exts.OES_texture_half_float_linear ? GL_HALF_FLOAT_OES : GL_UNSIGNED_BYTE;
  if (!light_target_init(&cache->emission_texture, &cache->emission_framebuffer, width, height, type)) {
    return false;
  }
  const int w = (full_width + 1) / 2, h = (full_height + 1) / 2;
  for (int i = 0; i <= levels; i++) {
    cache->widths[i] = (w + (1 << i) - 1) >> i;
    cache->heights[i] = (h + (1 << i) - 1) >> i;
    if (cache->widths[i] < 1) cache->widths[i] = 1;
    if (cache->heights[i] < 1) cache->heights[i] = 1;
    if (!light_target_init(&cache->textures[i], &cache->framebuffers[i], cache->widths[i], cache->heights[i], type)) {
      return false;
    }
  }
  cache->failed = false;
  return true;
}

static void light_blur(struct fx_effect_light_cache* cache, int source, int target, struct blur_shader* shader, float offset, bool down) {
  glBindFramebuffer(GL_FRAMEBUFFER, cache->framebuffers[target]);
  glViewport(0, 0, cache->widths[target], cache->heights[target]);
  glUseProgram(shader->program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, cache->textures[source]);
  glUniform1i(shader->tex, 0);
  glUniform1f(shader->radius, offset);
  const float ws = cache->widths[source], hs = cache->heights[source];
  glUniform2f(shader->halfpixel, 0.5f / ws, 0.5f / hs);
  glUniform4f(shader->sample_bounds, 0.5f / ws, 0.5f / hs, 1 - 0.5f / ws, 1 - 0.5f / hs);
  float projection[9];
  matrix_projection(projection, cache->widths[target], cache->heights[target], WL_OUTPUT_TRANSFORM_FLIPPED_180);
  const struct wlr_box box = {.width = cache->widths[target], .height = cache->heights[target]};
  set_proj_matrix(shader->proj, projection, &box);
  // The Kawase shaders expect the level ratio in the UV scale.
  const struct wlr_fbox uv = {.width = down ? 0.5 : 2, .height = down ? 0.5 : 2};
  set_tex_matrix(shader->tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &uv);
  render(&box, NULL, shader->pos);
}
```
(Confirm the `blur_shader` member names against `internal/render/fx_renderer/shaders.h`; `render_blur_segments` at `fx_pass.c:1859` shows how they are set today.)

The emission hook (replaces the Task 4.1 stub), called from `fx_render_pass_end_effect` with the popped capture texture still alive:

```c
// Renders the slot's program a second time into the emission texture, then
// thresholds it into level 0 and blurs the pyramid. Restores the pass target.
static void emit_light(
    struct fx_gles_render_pass* pass, const struct fx_effect_composite* composite, struct wlr_texture* texture,
    struct wlr_texture* previous_texture, const struct wlr_box* previous_box, const struct wlr_box* box,
    const struct wlr_box* logical_box, const struct fx_effect_geometry* geometry
) {
  struct fx_effect_light_cache* cache = composite->light;
  struct fx_renderer* renderer = pass->buffer->renderer;
  const struct fx_effect_light* light = &composite->parameters->light;
  if (cache->renderer != renderer || !ensure_light_program(renderer) || box->width <= 0 || box->height <= 0) {
    return;
  }
  const float scale = logical_box->width > 0 ? (float)box->width / logical_box->width : 1;
  const float spread_px = light->spread * scale;
  const int margin = (int)ceilf(spread_px * 2 + 8);
  if (!light_cache_prepare(cache, box->width, box->height, margin, spread_px)) {
    wlr_log(WLR_ERROR, "Cannot allocate effect light buffers; keeping the plain border");
    return;
  }
  // 1. Program into the emission texture, unblended, no history write.
  glBindFramebuffer(GL_FRAMEBUFFER, cache->emission_framebuffer);
  glViewport(0, 0, box->width, box->height);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  float projection[9];
  matrix_projection(projection, box->width, box->height, WL_OUTPUT_TRANSFORM_FLIPPED_180);
  const struct wlr_box local = {.width = box->width, .height = box->height};
  draw_animation_texture(
      pass, texture, composite->shader, composite->parameters, &local, box, logical_box, composite->transform, NULL,
      previous_texture, previous_box, projection, false, false, composite->expand, geometry
  );
  // 2. Threshold into level 0 (half resolution, margin around).
  glBindFramebuffer(GL_FRAMEBUFFER, cache->framebuffers[0]);
  glViewport(0, 0, cache->widths[0], cache->heights[0]);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(renderer->effect_light_program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, cache->emission_texture);
  glUniform1i(renderer->effect_light_tex, 0);
  glUniform1i(renderer->effect_light_emission, true);
  glUniform1i(renderer->effect_light_source_linear, pass->has_color_transform);
  glUniform1f(renderer->effect_light_threshold, light->threshold);
  const float full_w = box->width + 2.0f * margin, full_h = box->height + 2.0f * margin;
  glUniform4f(renderer->effect_light_source_region, margin / full_w, margin / full_h, box->width / full_w, box->height / full_h);
  float level_projection[9];
  matrix_projection(level_projection, cache->widths[0], cache->heights[0], WL_OUTPUT_TRANSFORM_FLIPPED_180);
  const struct wlr_box level = {.width = cache->widths[0], .height = cache->heights[0]};
  set_proj_matrix(renderer->effect_light_proj, level_projection, &level);
  const struct wlr_fbox unit = {.width = 1, .height = 1};
  set_tex_matrix(renderer->effect_light_tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &unit);
  glDisable(GL_BLEND);
  render(&level, NULL, renderer->effect_light_pos);
  // 3. Kawase down then up.
  const float offset = (spread_px * 0.5f) / (3 * ((1 << cache->levels) - 1));
  for (int i = 1; i <= cache->levels; i++) light_blur(cache, i - 1, i, &renderer->shaders.blur1, offset, true);
  for (int i = cache->levels; i > 0; i--) light_blur(cache, i, i - 1, &renderer->shaders.blur2, offset, false);
  cache->valid = true;
  glEnable(GL_BLEND);
  fx_framebuffer_bind(pass->buffer);
  glViewport(0, 0, pass->buffer->buffer->width, pass->buffer->buffer->height);
}

void fx_render_pass_add_effect_light(
    struct fx_gles_render_pass* pass, struct fx_effect_light_cache* cache, const struct fx_effect_light* light,
    const struct wlr_box* box, const pixman_region32_t* clip
) {
  struct fx_renderer* renderer = pass->buffer->renderer;
  if (cache == NULL || !cache->valid || cache->renderer != renderer || !ensure_light_program(renderer)) {
    return;
  }
  glUseProgram(renderer->effect_light_program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, cache->textures[0]);
  glUniform1i(renderer->effect_light_tex, 0);
  glUniform1i(renderer->effect_light_emission, false);
  glUniform1f(renderer->effect_light_gain, light->intensity);
  glUniform1i(renderer->effect_light_linear, pass->has_color_transform);
  set_proj_matrix(renderer->effect_light_proj, pass->projection_matrix, box);
  const struct wlr_fbox uv = {.width = 1, .height = 1};
  set_tex_matrix(renderer->effect_light_tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &uv);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
  render_pass_mark_updated(pass, box, clip);
  render(box, clip, renderer->effect_light_pos);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBindTexture(GL_TEXTURE_2D, 0);
}
```
Transform note: the pyramid is built in buffer orientation (the emission draw uses `composite->transform` exactly as the on-screen composite does), so the blend draws with `WL_OUTPUT_TRANSFORM_NORMAL` uv.

- [ ] **Step 5: Scene side (`wlr_scene.c`)**

Add to `scene_effects`: `struct wlr_scene_tree* light_layer; struct wl_listener light_layer_destroy;` and to `scene_animation`: `struct scene_light* light;`. Then:

```c
struct scene_light {
  struct wlr_addon addon;
  struct wlr_scene_rect* rect;
  struct scene_animation* source;
  struct fx_effect_light_cache* cache;
  int margin; // logical
};

static void scene_light_destroy(struct wlr_addon* addon) {
  struct scene_light* light = wl_container_of(addon, light, addon);
  if (light->source != NULL) {
    light->source->light = NULL;
  }
  fx_effect_light_cache_destroy(light->cache);
  wlr_addon_finish(addon);
  free(light);
}

static const struct wlr_addon_interface scene_light_impl = {.name = "scene_effect_light", .destroy = scene_light_destroy};

static struct scene_light* scene_light_from_node(struct wlr_scene_node* node) {
  struct wlr_addon* addon = wlr_addon_find(&node->addons, &scene_light_impl, &scene_light_impl);
  if (addon == NULL) {
    return NULL;
  }
  struct scene_light* light = wl_container_of(addon, light, addon);
  return light;
}

static void scene_light_remove(struct scene_animation* animation) {
  if (animation->light != NULL) {
    struct wlr_scene_rect* rect = animation->light->rect;
    animation->light->source = NULL;
    animation->light = NULL;
    wlr_scene_node_destroy(&rect->node); // destroys the addon with it
  }
}

// True when `node`'s root-level ancestor sorts above `layer` among the scene
// root's children. Children list order is bottom to top (raise_to_top appends),
// so meeting the layer before the ancestor means the ancestor is above it.
static bool node_above_layer(struct wlr_scene_node* node, struct wlr_scene_tree* layer) {
  struct wlr_scene* scene = scene_node_get_root(node);
  struct wlr_scene_node* top = node;
  while (top->parent != NULL && top->parent != &scene->tree) {
    top = &top->parent->node;
  }
  struct wlr_scene_node* child;
  wl_list_for_each(child, &scene->tree.children, link) {
    if (child == top) {
      return false; // reached the ancestor first: it is below the layer
    }
    if (child == &layer->node) {
      return true; // reached the layer first: the ancestor sits above it
    }
  }
  return false;
}

static bool transient_ancestor(struct wlr_scene_node* node) {
  for (struct wlr_scene_tree* parent = node->parent; parent != NULL; parent = parent->node.parent) {
    struct scene_animation* animation = scene_animation_get(&parent->node);
    if (animation != NULL && animation->transient) {
      return true;
    }
  }
  return false;
}

// Creates, positions, or removes the light proxy for a border slot. Called
// whenever the slot, the layer, or the node's placement changes.
static void scene_light_sync(struct scene_animation* animation) {
  struct scene_effects* effects = scene_effects_get(animation->scene, false);
  const struct fx_animation_parameters* parameters = &animation->parameters[FX_SLOT_BORDER_EFFECT];
  int lx, ly;
  const bool wanted = effects != NULL && effects->light_layer != NULL
      && animation->shaders[FX_SLOT_BORDER_EFFECT] != NULL && parameters->light.enabled
      && parameters->light.intensity > 0 && parameters->light.spread > 0
      && wlr_scene_node_coords(animation->node, &lx, &ly)
      && !transient_ancestor(animation->node) && !node_above_layer(animation->node, effects->light_layer);
  if (!wanted) {
    scene_light_remove(animation);
    return;
  }
  struct scene_light* light = animation->light;
  if (light == NULL) {
    light = calloc(1, sizeof(*light));
    if (light == NULL) {
      return;
    }
    // The rect only tracks visibility and damage; the addon renders in its place.
    const float color[4] = {0, 0, 0, 0.5f};
    light->rect = wlr_scene_rect_create(effects->light_layer, 1, 1, color);
    if (light->rect == NULL) {
      free(light);
      return;
    }
    light->rect->accepts_input = false;
    light->source = animation;
    light->cache = fx_effect_light_cache_create(animation->shaders[FX_SLOT_BORDER_EFFECT]->renderer);
    wlr_addon_init(&light->addon, &light->rect->node.addons, &scene_light_impl, &scene_light_impl);
    animation->light = light;
  }
  pixman_region32_t bounds;
  pixman_region32_init(&bounds);
  scene_node_bounds(animation->node, lx, ly, &bounds);
  const pixman_box32_t* extents = pixman_region32_extents(&bounds);
  const int expand = animation_expand(animation);
  light->margin = (int)ceilf(parameters->light.spread * 2 + 8) + expand;
  int layer_x, layer_y;
  wlr_scene_node_coords(&effects->light_layer->node, &layer_x, &layer_y);
  wlr_scene_node_set_position(&light->rect->node, extents->x1 - light->margin - layer_x, extents->y1 - light->margin - layer_y);
  wlr_scene_rect_set_size(light->rect, extents->x2 - extents->x1 + 2 * light->margin, extents->y2 - extents->y1 + 2 * light->margin);
  pixman_region32_fini(&bounds);
}

static void scene_effects_light_layer_destroy(struct wl_listener* listener, void* data) {
  struct scene_effects* effects = wl_container_of(listener, effects, light_layer_destroy);
  wl_list_remove(&effects->light_layer_destroy.link);
  effects->light_layer = NULL;
  struct scene_animation* animation;
  wl_list_for_each(animation, &effects->animations, link) { scene_light_remove(animation); }
}

void wlr_scene_set_effect_light_layer(struct wlr_scene* scene, struct wlr_scene_tree* layer) {
  struct scene_effects* effects = scene_effects_get(scene, layer != NULL);
  if (effects == NULL) {
    return;
  }
  if (effects->light_layer != NULL) {
    wl_list_remove(&effects->light_layer_destroy.link);
  }
  effects->light_layer = layer;
  if (layer != NULL) {
    effects->light_layer_destroy.notify = scene_effects_light_layer_destroy;
    wl_signal_add(&layer->node.events.destroy, &effects->light_layer_destroy);
  }
  struct scene_animation* animation;
  wl_list_for_each(animation, &effects->animations, link) { scene_light_sync(animation); }
}
```
Wire it:
- `wlr_scene_node_set_animation`: after the classify/recount block, `scene_light_sync(animation)` (before the possible destroy; skip when `!populated`).
- `scene_animation_destroy`: `scene_light_remove(animation)` before removing from the list. `scene_effects_destroy` must also drop the layer listener (`if (effects->light_layer) wl_list_remove(&effects->light_layer_destroy.link)`). Stage 1's `scene_effects_destroy` unlinks any animations still listed (scene teardown finishes the root's addons before its children); keep that, and change the "destroy when the list empties" rule in `scene_animation_destroy` to "destroy when empty and `light_layer == NULL`" so the addon persists while a light layer is registered.
- `scene_node_update`: when `scene_effects_get(scene, false)` returns state with `persistent > 0`, walk `effects->animations` and `scene_light_sync` every animation whose `node` `node_belongs_to(animation->node, node)` and has a light or wants one (cheap: only animations with `parameters[BORDER_EFFECT].light.enabled`). (`scene_has_animations`/`scene_has_persistent_effects` no longer exist; Stage 1 resolves the addon once per caller.)
- `scene_effect_damage`: after damaging the node bounds, if `animation->light != NULL` (look up via `scene_animation_get(node)`), damage the proxy: `wlr_scene_node_coords(&rect->node, &rx, &ry)`, region `(rx, ry, rect->width, rect->height)`, `scene_damage_outputs`.
- `persistent_effect_box` (Stage 1's whole-box invalidation): when `animation->light != NULL`, union the proxy rect's box (its coords minus `data->logical`, transformed with `transform_output_box`) into `*box` so damage touching the spill invalidates the source and the spill together.
- `render_animated_range`: in the composite construction set `.light = slot == FX_SLOT_BORDER_EFFECT && animation->light != NULL && !data->shadow_capture ? animation->light->cache : NULL`.
- `scene_entry_render`, `case WLR_SCENE_NODE_RECT:` — first thing:
  ```c
    if (struct scene_light* light = scene_light_from_node(node); ...)  // C: declare before the switch
  ```
  Write it as: before the `switch (node->type)`, `struct scene_light* light = node->type == WLR_SCENE_NODE_RECT ? scene_light_from_node(node) : NULL;` and in the RECT case:
  ```c
    if (light != NULL) {
      if (light->source != NULL && light->source->shaders[FX_SLOT_BORDER_EFFECT] != NULL) {
        fx_render_pass_add_effect_light(
            fx_pass, light->cache, &light->source->parameters[FX_SLOT_BORDER_EFFECT].light, &dst_box, &render_region
        );
      }
      break;
    }
  ```
  The proxy box (`dst_box`) equals the emission box plus margin in buffer pixels only when the border is unscaled; for scaled outputs the cache's own margin (`ceil(spread_px*2+8)`) and the proxy's logical margin agree after scaling up to rounding — accept the ≤1px seam.
- `wlr_scene_node_copy_animations_for_snapshot`: already clears `light.enabled` on the copy (Stage 1).

- [ ] **Step 6: Run tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs'`
Expected: `effects-border-light` passes along with every other case.

- [ ] **Step 7: Commit**

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): border light from effect slot emission"
```

---

### Task 4.3: Ring padding in `makeBorderRing`, decoration, snapshots, overview

**Files:**
- Modify: `src/view/border_ring.h`, `src/view/border_ring.cpp`, `src/scene/border_rect.h` (`BorderSnapshot::padding`), `src/view/decoration.h`, `src/view/decoration.cpp:41-108`, `src/server/server.cpp:1090-1105` (`CloseSnapshot::applyShrink`), `src/overview/overview.cpp:280-290`
- Test: `tests/unit/border_ring.cpp`

**Interfaces:**
- Produces: `BorderRing makeBorderRing(int contentWidth, int contentHeight, int outerRadius, int innerWidth, int outerWidth, int padding = 0);` — `padding` grows `box` outward and moves `hole` by the same amount; radii are unchanged.
- Produces: `bool ViewDecoration::setBorderPadding(int padding);` (true when changed), `int ViewDecoration::borderPadding() const;`, `BorderSnapshot::padding`.
- Produces: `int View::borderEffectPadding() const` (public; the resolved border preset's padding when it applies to this view, else 0) — used by the overview.

- [ ] **Step 1: Write the failing unit test**

Append to `tests/unit/border_ring.cpp`:

```cpp
UMBRIEL_TEST(paddingGrowsTheRingBoxAroundTheSameHole) {
  const auto plain = makeBorderRing(200, 120, 10, 4, 0);
  const auto padded = makeBorderRing(200, 120, 10, 4, 0, 16);
  CHECK_EQ(padded.box.x, plain.box.x - 16);
  CHECK_EQ(padded.box.y, plain.box.y - 16);
  CHECK_EQ(padded.box.width, plain.box.width + 32);
  CHECK_EQ(padded.box.height, plain.box.height + 32);
  CHECK_EQ(padded.hole.x, plain.hole.x + 16);
  CHECK_EQ(padded.hole.y, plain.hole.y + 16);
  CHECK_EQ(padded.hole.width, plain.hole.width);
  CHECK_EQ(padded.hole.height, plain.hole.height);
  CHECK_EQ(padded.inner.top_left, plain.inner.top_left);
  CHECK_EQ(padded.outer.top_left, plain.outer.top_left);
}
```

Run: `nix develop . --command bash -c 'meson compile -C build-debug border-ring-test 2>&1 | tail -2'` — Expected: compile error (too many arguments).

- [ ] **Step 2: Implement**

`border_ring.h`: `makeBorderRing(int contentWidth, int contentHeight, int outerRadius, int innerWidth, int outerWidth, int padding = 0);` with the comment "`padding` is transparent space around the ring that a border effect may paint into."

`border_ring.cpp`:
```cpp
    const int extent = thickness + renderMargin + (padding > 0 ? padding : 0);
```
and the `box`/`hole` use `extent` as today (both already do).

`border_rect.h`: add `int padding = 0;` to `BorderSnapshot` after `cornerRadius` with the comment "Effect padding the ring was drawn with."

`decoration.h`: add
```cpp
    // Transparent margin a border effect paints into. True when it changed.
    bool setBorderPadding(int padding);
    [[nodiscard]] int borderPadding() const { return m_borderPadding; }
```
and `int m_borderPadding = 0;` among the private members. `decoration.cpp`: `updateBorderGeometry` and `borderGeometryStale` pass `m_borderPadding` as the sixth argument; `snapshotBorders` sets `.padding = m_borderPadding` in `captured`; add
```cpp
  bool ViewDecoration::setBorderPadding(int padding) {
    if (padding == m_borderPadding) {
      return false;
    }
    m_borderPadding = padding;
    return true;
  }
```
`server.cpp` `CloseSnapshot::applyShrink`: `const int padding = static_cast<int>(std::lround(captured.padding * ringScale));` and `makeBorderRing(width, height, radius, innerWidth, outerWidth, padding)`.

`overview.cpp` `layoutCard` (border block): `const int padding = scaledWidth(view->borderEffectPadding());` and pass it to `makeBorderRing(contentW, contentH, outerRadius, innerWidth, outerWidth, padding)`. (`View::borderEffectPadding()` is added in Task 4.4; add a stub returning 0 now so this compiles: `[[nodiscard]] int borderEffectPadding() const { return m_decoration.borderPadding(); }` in `view.h`.)

- [ ] **Step 3: Run tests and checks**

Run: `nix develop . --command bash -c 'just test && just check 18 72 73 74'`
Expected: unit tests pass; border/decoration checks (`72x` corner and border checks, `183_border_focus_transition`) pass unchanged (padding is 0 everywhere).

- [ ] **Step 4: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(view): border ring padding for border effects"
```

---

### Task 4.4: `ViewEffects`, `View` integration, light layer, config apply

**Files:**
- Create: `src/view/effects.h`, `src/view/effects.cpp`
- Modify: `src/view/view.h`, `src/view/view.cpp` (`syncAnimationShaders` :1319-1360, `applyDynamicRules` :4594, `~View`, `handleUnmap` path), `src/overview/overview.cpp:220-222,280-300`, `src/scene/effect_registry.h/.cpp` (`updateInstance`, `removeInstance`, `ensureLightLayer`), `src/server/server.h/.cpp/server_events.cpp` (light layer, `applyConfig`), `meson.build` (`pure_sources` + `core_sources`)
- Test: `tests/unit/effects.cpp`

**Interfaces:**
- Produces (`src/view/effects.h`):
  ```cpp
  struct ViewEffectNames { std::string border; std::string window; };
  [[nodiscard]] ViewEffectNames resolveViewEffectNames(const Effects& effects, const ResolvedWindowRule& rule);
  struct BorderEffectGate { bool focused = false; bool decorated = false; bool urgent = false; bool fullscreen = false; };
  [[nodiscard]] bool borderEffectApplies(const BorderEffectGate& gate);
  class ViewEffects {
  public:
    void resolve(const Effects& effects, const ResolvedWindowRule& rule);
    [[nodiscard]] const std::string& borderName() const;
    [[nodiscard]] const std::string& windowName() const;
    [[nodiscard]] int borderPadding() const;   // preset padding, 0 when no usable border preset
    struct ApplyInput {
      wlr_scene_node* surface = nullptr;   // Stage 5
      wlr_scene_node* border = nullptr;
      BorderEffectGate gate;
      float seconds = 0.0F;                // animation clock; only read when an effect is configured
      bool clockAdvancing = true;
      const void* output = nullptr;        // the output driving this instance's frames
      wlr_box outputBox{};                 // that output's layout box: an instance is visible only inside it
    };
    // True when a border or window preset is selected for this view: the caller reads the clock only then.
    [[nodiscard]] bool configured() const { return !m_border.empty() || !m_window.empty(); }
    void apply(const ApplyInput& input);
    // Ledger removal for every node this object registered; slots are cleared by the caller.
    void detach();
    // Ledger removal for one set of nodes (an overview card being destroyed).
    void detachNodes(wlr_scene_node* surface, wlr_scene_node* border);
  private:
    void track(const void* owner);     // remember a ledger owner so detach() can drop it
    void untrack(const void* owner);   // remove from the ledger and forget it; null is a no-op
    std::string m_border;
    std::string m_window;
    std::vector<const void*> m_owners;   // nodes registered in the ledger
  };
  ```
  (`effects.h` includes `<string>` and `<vector>`; `effects.cpp` includes `<algorithm>` for `std::ranges::find`/`std::erase`.)
  Ledger instances are keyed by the scene node that carries the slot (the border node, the surface node), not by the view: a view's live trees and its overview cards are separate instances with their own visibility and output, so a card keeps animating while the live window is hidden. Visibility is the node's own `visible` region intersected with the driving output's layout box: the scene keeps `visible` empty for disabled, clipped, off-workspace, and fully occluded nodes, and the intersection excludes a window that has left its output — exactly the cases the spec lists as not requesting frames. Light spill does not add instances: the spill is drawn from the proxy rect wherever it is visible, and its damage follows the border's.
- Produces (`EffectRegistry`): `void updateInstance(const void* owner, const EffectInstanceState& state);` `void removeInstance(const void* owner);` `void ensureLightLayer();` (creates the layer through `Server::ensureEffectLightLayer()` when any referenced border preset has `light`).
- Produces (`Server`): `wlr_scene_tree* ensureEffectLightLayer();` — lazily `wlr_scene_tree_create(&m_scene->tree)`, `wlr_scene_node_place_above(&tree->node, &m_dragIconTree->node)`, `wlr_scene_set_effect_light_layer(m_scene, tree)`; member `wlr_scene_tree* m_effectLightTree = nullptr;`.
- Produces (`View`): `void syncAnimationShaders(wlr_scene_tree* target = nullptr, wlr_scene_node* border = nullptr, wlr_scene_node* surface = nullptr, const BorderEffectGate* gate = nullptr);` `int borderEffectPadding() const;` `ViewEffects m_effects;`.
- Produces (`Output`): `void scheduleEffectFrame();` (Task 4.5 fills it; add a declaration and a one-line `wlr_output_schedule_frame(m_output)` body now).

- [ ] **Step 1: Write the failing pure tests**

Append to `tests/unit/effects.cpp`:

```cpp
#include "config/config.h"
#include "view/effects.h"

UMBRIEL_TEST(viewEffectNamesFollowTheMostSpecificSelector) {
  umbriel::Effects effects;
  effects.border = "pulse";
  effects.window = "lines";
  umbriel::ResolvedWindowRule rule;
  auto names = umbriel::resolveViewEffectNames(effects, rule);
  CHECK_EQ(names.border, std::string("pulse"));
  CHECK_EQ(names.window, std::string("lines"));
  rule.borderEffect = "off";
  rule.windowEffect = "scan";
  names = umbriel::resolveViewEffectNames(effects, rule);
  CHECK(names.border.empty());
  CHECK_EQ(names.window, std::string("scan"));
  rule.borderEffect = "";
  names = umbriel::resolveViewEffectNames(effects, rule);
  CHECK(names.border.empty());
}

UMBRIEL_TEST(borderEffectsApplyOnlyToFocusedDecoratedCalmWindows) {
  CHECK(umbriel::borderEffectApplies({.focused = true, .decorated = true, .urgent = false, .fullscreen = false}));
  CHECK(!umbriel::borderEffectApplies({.focused = false, .decorated = true, .urgent = false, .fullscreen = false}));
  CHECK(!umbriel::borderEffectApplies({.focused = true, .decorated = false, .urgent = false, .fullscreen = false}));
  CHECK(!umbriel::borderEffectApplies({.focused = true, .decorated = true, .urgent = true, .fullscreen = false}));
  CHECK(!umbriel::borderEffectApplies({.focused = true, .decorated = true, .urgent = false, .fullscreen = true}));
}
```

`tests/unit/effects.cpp` links only `umbriel_pure_dep`; keep `resolveViewEffectNames` and `borderEffectApplies` in `src/view/effects_rules.cpp`? No — simpler: put both pure functions in `src/view/effects.cpp` and add that file to `pure_sources`; `ViewEffects::apply` needs the registry (core). Split: `src/view/effects.h` declares everything; the pure functions live in `src/view/effects_rules.cpp` (pure_sources), the class in `src/view/effects.cpp` (core_sources). Adjust the file list accordingly.

Run: `nix develop . --command bash -c 'meson compile -C build-debug effects-test 2>&1 | tail -2'` — Expected: compile error (`view/effects.h` missing).

- [ ] **Step 2: Implement the pure part**

`src/view/effects_rules.cpp`:
```cpp
#include "view/effects.h"

namespace umbriel {

  ViewEffectNames resolveViewEffectNames(const Effects& effects, const ResolvedWindowRule& rule) {
    // The most specific selector replaces the default by name; "off" and "" both disable it.
    const auto pick = [](const std::string& fallback, const std::optional<std::string>& override) {
      if (!override) {
        return fallback;
      }
      return *override == kEffectOff ? std::string() : *override;
    };
    return {.border = pick(effects.border, rule.borderEffect), .window = pick(effects.window, rule.windowEffect)};
  }

  bool borderEffectApplies(const BorderEffectGate& gate) {
    return gate.focused && gate.decorated && !gate.urgent && !gate.fullscreen;
  }

} // namespace umbriel
```
Header per the interface block (include `config/config.h`, `config/effects.h`; forward-declare `wlr_scene_node`).

Run the two tests: `nix develop . --command bash -c 'meson test -C build-debug effects --print-errorlogs'` — Expected: pass.

- [ ] **Step 3: Implement `ViewEffects::apply` and the registry hooks**

`src/view/effects.cpp`:
```cpp
#include "view/effects.h"

#include "scene/effect_registry.h"

extern "C" {
#include <umbrielfx/render/effect.h>
}

namespace umbriel {

  void ViewEffects::resolve(const Effects& effects, const ResolvedWindowRule& rule) {
    const ViewEffectNames names = resolveViewEffectNames(effects, rule);
    m_border = names.border;
    m_window = names.window;
  }

  int ViewEffects::borderPadding() const {
    if (m_border.empty()) {
      return 0;
    }
    const EffectPreset* preset = effectRegistry().presetConfig(m_border);
    return preset != nullptr && preset->kind == EffectKind::Border && !preset->inert() ? preset->padding : 0;
  }

  namespace {
    // Whether any of the node's leaves is visible inside `box` (layout coordinates). The scene keeps a leaf's
    // `visible` region empty when it is disabled, clipped, off-workspace, or fully occluded.
    bool nodeVisibleOn(const wlr_scene_node* node, const wlr_box& box) {
      if (node == nullptr || !node->enabled) {
        return false;
      }
      if (node->type != WLR_SCENE_NODE_TREE) {
        pixman_region32_t onOutput;
        pixman_region32_init(&onOutput);
        pixman_region32_intersect_rect(&onOutput, &node->visible, box.x, box.y, box.width, box.height);
        const bool visible = pixman_region32_not_empty(&onOutput);
        pixman_region32_fini(&onOutput);
        return visible;
      }
      const wlr_scene_node* child = nullptr;
      wl_list_for_each(child, &wlr_scene_tree_from_node(const_cast<wlr_scene_node*>(node))->children, link) {
        if (nodeVisibleOn(child, box)) {
          return true;
        }
      }
      return false;
    }
  } // namespace

  void ViewEffects::track(const void* owner) {
    if (std::ranges::find(m_owners, owner) == m_owners.end()) {
      m_owners.push_back(owner);
    }
  }

  void ViewEffects::untrack(const void* owner) {
    if (owner == nullptr) {
      return;
    }
    effectRegistry().removeInstance(owner);
    std::erase(m_owners, owner);
  }

  void ViewEffects::apply(const ApplyInput& input) {
    EffectRegistry& registry = effectRegistry();
    // The merge gate: with nothing selected this is two string checks.
    if (!configured()) {
      if (registry.active() > 0) {
        untrack(input.border);
        untrack(input.surface);
        if (input.border != nullptr) {
          wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, nullptr, nullptr);
        }
      }
      return;
    }
    const EffectPreset* preset = m_border.empty() ? nullptr : registry.presetConfig(m_border);
    fx_effect_shader* shader = preset != nullptr ? registry.preset(m_border, EffectKind::Border) : nullptr;
    const bool active = shader != nullptr && input.border != nullptr && borderEffectApplies(input.gate);
    if (input.border != nullptr) {
      if (active) {
        fx_animation_parameters parameters{};
        const bool advancing = preset->animated && preset->speed > 0.0F;
        registry.fillTimeUniforms(parameters, advancing ? input.seconds * preset->speed : 0.0F, *preset, shader);
        if (preset->light) {
          parameters.light = {
              .enabled = true,
              .spread = static_cast<float>(preset->light->spread),
              .intensity = preset->light->intensity,
              .threshold = preset->light->threshold,
          };
        }
        wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, shader, &parameters);
      } else {
        wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, nullptr, nullptr);
      }
    }
    if (active) {
      track(input.border);
      registry.updateInstance(
          input.border,
          {
              .output = input.output,
              .visible = nodeVisibleOn(input.border, input.outputBox),
              .readsTime = fx_effect_shader_reads(shader, "umbriel_time"),
              .advancing = preset->animated && preset->speed > 0.0F && input.clockAdvancing,
          }
      );
    } else {
      untrack(input.border);
    }
  }

  void ViewEffects::detach() {
    EffectRegistry& registry = effectRegistry();
    for (const void* owner : m_owners) {
      registry.removeInstance(owner);
    }
    m_owners.clear();
  }

  void ViewEffects::detachNodes(wlr_scene_node* surface, wlr_scene_node* border) {
    untrack(surface);
    untrack(border);
  }

} // namespace umbriel
```

`EffectRegistry` additions (`effect_registry.cpp`):
```cpp
  void EffectRegistry::updateInstance(const void* owner, const EffectInstanceState& state) {
    const unsigned before = m_ledger.eligible(state.output);
    m_ledger.update(owner, state);
    if (before == 0 && m_ledger.eligible(state.output) > 0) {
      for (const auto& output : m_server->outputs()) {
        if (output.get() == state.output) {
          output->scheduleEffectFrame();
        }
      }
    }
  }

  void EffectRegistry::removeInstance(const void* owner) { m_ledger.remove(owner); }

  void EffectRegistry::ensureLightLayer() {
    for (const EffectPreset& preset : config().effects.presets) {
      if (preset.kind == EffectKind::Border && preset.light && m_programs.contains(preset.name)) {
        m_server->ensureEffectLightLayer();
        return;
      }
    }
  }
```
Call `ensureLightLayer()` at the end of `prepare()`. `Output::scheduleEffectFrame()` is declared in `output.h` (public) with the body `wlr_output_schedule_frame(m_output);` for now (Task 4.5 replaces it). Add `void removeOutput(const Output*)` → `m_ledger.removeOutput(output)`, called from `Server::removeOutput` (server_events.cpp ~1874, next to the cursor reset).

`Server`:
```cpp
  wlr_scene_tree* Server::ensureEffectLightLayer() {
    if (m_effectLightTree == nullptr) {
      // Ring illumination stays below panels and pinned content, above dragged windows.
      m_effectLightTree = wlr_scene_tree_create(&m_scene->tree);
      wlr_scene_node_place_above(&m_effectLightTree->node, &m_dragIconTree->node);
      wlr_scene_set_effect_light_layer(m_scene, m_effectLightTree);
    }
    return m_effectLightTree;
  }
```
(A drag of a pinned window moves `m_dragTree` above `m_pinnedTree` — the light layer stays put, which is the spec's "borders stacked above the light layer do not emit".)

`View`:
- `view.h`: `#include "view/effects.h"`; member `ViewEffects m_effects;` next to `m_decoration`; the new `syncAnimationShaders` signature; `[[nodiscard]] int borderEffectPadding() const { return m_effects.borderPadding(); }` replacing the Task 4.3 stub.
- `view.cpp` `syncAnimationShaders`: the `!m_mapped` branch also calls `m_effects.detach();`. After `updateAnimationShader(border, renderer, AnimationEvent::Border, ...)`, add:
  ```cpp
    // Persistent effects. With none configured this costs one string check per slot and never reads the clock.
    if (m_effects.configured() || effectRegistry().active()) {
      const BorderEffectGate ownGate{
          .focused = m_borderFocusedState,
          .decorated = decorated(),
          .urgent = m_urgent,
          .fullscreen = m_toplevel->scheduled.fullscreen,
      };
      Output* output = cardOutput != nullptr ? cardOutput : currentOutput();
      m_effects.apply({
          .surface = surface,
          .border = border,
          .gate = gate != nullptr ? *gate : ownGate,
          .seconds = m_effects.configured() ? effectRegistry().clockSeconds() : 0.0F,
  #ifdef UMBRIEL_TEST_IPC
          .clockAdvancing = !m_server->animationClockFrozen(),
  #endif
          .output = output,
          .outputBox = output != nullptr ? output->layoutBox() : wlr_box{},
      });
    }
  ```
  and when `target == nullptr` (the view's own trees) also default `surface = toplevelSurfaceTreeNode(m_contentTree, m_toplevel->base->surface)`. The signature gains a fifth parameter `Output* cardOutput = nullptr` (the overview passes `card.owner->output`); `EffectRegistry::clockSeconds()` is the helper Stage 3 added. Spanning outputs need no second instance: `wlr_scene_node_set_animation` damages the slot's bounds on every output they intersect, so the driving output's timer feeds the others through ordinary damage.
- `applyDynamicRules`: after `if (m_decoration.applyRule(rule)) {...}`:
  ```cpp
    m_effects.resolve(config().effects, rule);
    if (m_decoration.setBorderPadding(m_effects.borderPadding())) {
      updateBorderGeometry();
      applyCornerRadius();
      updateShadow();
    }
  ```
- `~View()`: `m_effects.detach();`.
- `Overview`: one card-effect sync used from two places, because `Overview::tickAnimations` (`overview.cpp:1747`) never relayouts cards — a card's time uniforms would otherwise stop after layout settles while its ledger instance keeps requesting frames. Add to `overview.h` (private) `void syncCardEffects(Card& card);` and `void syncCardEffects();`:
  ```cpp
  void Overview::syncCardEffects(Card& card) {
    View* view = card.view;
    Workspace* workspace = view->workspace();
    const BorderEffectGate cardGate{
        .focused = workspace != nullptr && workspace->focusedView() == view && &card != m_dragCard,
        .decorated = card.border != nullptr && card.border->node.enabled,
        .urgent = view->urgent(),
        .fullscreen = view->toplevel()->current.fullscreen,
    };
    view->syncAnimationShaders(
        card.tree, card.border != nullptr ? &card.border->node : nullptr,
        card.surfaces.empty() ? nullptr : &card.surfaces.front()->buffer->node, &cardGate, card.owner->output
    );
  }

  void Overview::syncCardEffects() {
    for (auto& state : m_outputs) {   // whatever the per-output container is named in overview.h
      for (auto& card : state->cards) {
        syncCardEffects(*card);
      }
    }
  }
  ```
  `layoutCard` replaces its line-222 `syncAnimationShaders` call with `syncCardEffects(card)` placed after the border block (so `card.border->node.enabled` reflects this layout; the early-return paths call it before returning, which sheds the slot because the border is disabled there). `Overview::tickAnimations` calls `syncCardEffects()` once per tick while the overview is active and `effectRegistry().active() > 0` — the same gate `View::syncAnimationShaders` uses, so an overview without effects does no extra work. Use the focused predicate `cardBorderColor` already applies if it differs from the one above.

  Card nodes are their own ledger instances (keyed by node), so the card keeps requesting frames while the live window is hidden behind the overview. When the overview destroys a card (`Overview::destroyCard` or wherever `card.tree` is destroyed), call `card.view->effects().detachNodes(surfaceNode, &card.border->node)` first — add `[[nodiscard]] ViewEffects& effects() { return m_effects; }` to `View` (public, next to `syncAnimationShaders`).

- `Server::applyConfig`: after the `viewChrome` block:
  ```cpp
    if (effects.effects && !effects.viewChrome) {
      for (const auto& view : m_registry.all()) {
        if (view->mapped()) {
          view->applyDynamicRules();
        }
      }
    }
  ```
  and the existing `prepareAnimationShaders(m_renderer)` at the top already runs on `effects.effects`.

`meson.build`: `pure_sources` += `'src/view/effects_rules.cpp'`; `core_sources` += `'src/view/effects.cpp'`.

- [ ] **Step 4: Build and the border/focus checks**

Run: `nix develop . --command bash -c 'just build && just test && just check 18 72 73 74 31 32'`
Expected: green (no effect is configured in any of them; the merge-gate early return keeps behaviour identical).

- [ ] **Step 5: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(view): resolve and apply border effects per window"
```

---

### Task 4.5: Effect-only frames and the `effect-frames` test command

**Files:**
- Modify: `src/output/output.h`, `src/output/output.cpp` (`handleFrame` :987-1216, destructor :680), `src/server/ipc_commands.h`, `src/server/ipc_commands.cpp:650-677`, `src/server/server_events.cpp` (`activateSessionLock`, `unlockSession`), `docs/design/README.md:22-45`
- Test: `tests/unit/output_frame_schedule.cpp` (pure delay math)

**Interfaces:**
- Produces (`src/output/frame_schedule.h`): `[[nodiscard]] inline uint64_t effectFrameDelayMs(int maxFps, uint64_t nowMsec, uint64_t lastEffectFrameMsec)` — 0 when `maxFps == 0` (follow refresh); otherwise `max(1, 1000/maxFps - (now - last))`.
- Produces (`Output`): `void scheduleEffectFrame();` (public), private `static int onEffectFrameTimer(void*)`, `void armEffectFrame(uint64_t nowMsec)`, `wl_event_source* m_effectFrameTimer = nullptr; uint64_t m_lastEffectFrameMsec = 0; bool m_effectFrameDue = false; uint64_t m_effectFrames = 0;`, `[[nodiscard]] uint64_t effectFrames() const`, `[[nodiscard]] unsigned effectEligible() const`.
- Produces (IPC, `UMBRIEL_TEST_IPC`): `effect-frames` → `{"ok":{"outputs":[{"name":"HEADLESS-1","effect_frames":N,"eligible":K}]}}`.

- [ ] **Step 1: Write the failing unit test**

Append to `tests/unit/output_frame_schedule.cpp`:
```cpp
UMBRIEL_TEST(effectFrameDelayFollowsRefreshOrCapsAtMaxFps) {
  CHECK_EQ(umbriel::effectFrameDelayMs(0, 1000, 990), uint64_t{0});
  CHECK_EQ(umbriel::effectFrameDelayMs(60, 1000, 1000), uint64_t{16});
  CHECK_EQ(umbriel::effectFrameDelayMs(60, 1010, 1000), uint64_t{6});
  CHECK_EQ(umbriel::effectFrameDelayMs(60, 1100, 1000), uint64_t{1});
  CHECK_EQ(umbriel::effectFrameDelayMs(1, 1000, 1000), uint64_t{1000});
}
```
Run: `nix develop . --command bash -c 'meson compile -C build-debug output-frame-schedule-test 2>&1 | tail -2'` — Expected: compile error.

- [ ] **Step 2: Implement**

`frame_schedule.h`:
```cpp
  // Delay before the next effect-only frame. 0 follows the output's refresh (schedule immediately); otherwise the
  // interval for max_fps minus the time already elapsed, at least 1 ms so a late timer never spins.
  [[nodiscard]] inline uint64_t effectFrameDelayMs(int maxFps, uint64_t nowMsec, uint64_t lastEffectFrameMsec) {
    if (maxFps <= 0) {
      return 0;
    }
    const uint64_t interval = 1000 / static_cast<uint64_t>(maxFps);
    const uint64_t elapsed = nowMsec > lastEffectFrameMsec ? nowMsec - lastEffectFrameMsec : 0;
    return elapsed >= interval ? 1 : std::max<uint64_t>(1, interval - elapsed);
  }
```
(Add `#include <algorithm>` and `<cstdint>`.)

`output.cpp`:
```cpp
  int Output::onEffectFrameTimer(void* data) {
    auto* output = static_cast<Output*>(data);
    output->m_effectFrameDue = true;
    wlr_output_schedule_frame(output->m_output);
    return 0;
  }

  void Output::scheduleEffectFrame() {
    if (m_inFrame) {
      return; // handleFrame arms the follow-up itself
    }
    m_effectFrameDue = true;
    wlr_output_schedule_frame(m_output);
  }

  unsigned Output::effectEligible() const { return m_server->effects().ledger().eligible(this); }

  void Output::armEffectFrame(uint64_t nowMsec) {
    const uint64_t delay = effectFrameDelayMs(config().effects.maxFps, nowMsec, m_lastEffectFrameMsec);
    if (delay == 0) {
      m_effectFrameDue = true;
      wlr_output_schedule_frame(m_output);
      return;
    }
    if (m_effectFrameTimer == nullptr) {
      m_effectFrameTimer = wl_event_loop_add_timer(wl_display_get_event_loop(m_server->display()), onEffectFrameTimer, this);
    }
    wl_event_source_timer_update(m_effectFrameTimer, static_cast<int>(delay));
  }
```
In `handleFrame`:
- right after `const bool animationsActive = m_server->animationsActiveFor(this);` add
  ```cpp
    const bool effectFrame = m_effectFrameDue;
    m_effectFrameDue = false;
    // Persistent effects reading time keep an output drawing on their own timer, never through the animation
    // registry: settle, tearing, and the render lock keep their meanings.
    const bool effectsEligible = !m_server->sessionLocked() && effectEligible() > 0;
  ```
- inside `if (sceneChanged || m_gammaDirty) {` after `m_inFrame = true;` add `if (effectFrame) { ++m_effectFrames; m_lastEffectFrameMsec = m_server->animationClockMsec(); }` (use the monotonic clock: add a local `const uint64_t nowMsec = static_cast<uint64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;` from the `now` already read, and use it here and for `armEffectFrame`).
- after the `switch (outputFrameFollowup(...))` block add
  ```cpp
    if (effectsEligible && !animationsActive && !commitFailed) {
      armEffectFrame(nowMsec);
    } else if (m_effectFrameTimer != nullptr) {
      wl_event_source_timer_update(m_effectFrameTimer, 0);
    }
  ```
- destructor: `if (m_effectFrameTimer != nullptr) wl_event_source_remove(m_effectFrameTimer);`.

Session lock: in `Server::activateSessionLock` (first-activation block) add `m_effects->setSuspended(true);` and in `unlockSession` `m_effects->setSuspended(false);` then for every output `output->scheduleEffectFrame()` if `output->effectEligible() > 0`.

Clock freeze and resume (test builds): eligibility is recomputed only when a view syncs, which happens on a frame. `Server::freezeAnimationClock()` and `resumeAnimationClock()` (`server.cpp:1297-1353`) therefore end with `for (const auto& output : m_outputs) wlr_output_schedule_frame(output->wlr());` — the frame re-syncs every view with the new `clockAdvancing`, which disarms (freeze) or re-arms (resume, through `updateInstance`'s 0→1 poke) the effect timer. `advanceAnimationClock` already schedules frames on every output.

IPC (`ipc_commands.h`, inside `#ifdef UMBRIEL_TEST_IPC`): `static nlohmann::json effectFrames(Server& server, std::string_view arg);`. `ipc_commands.cpp`:
```cpp
#ifdef UMBRIEL_TEST_IPC
  nlohmann::json IpcCommands::effectFrames(Server& server, std::string_view /*arg*/) {
    nlohmann::json outputs = nlohmann::json::array();
    for (const auto& output : server.outputs()) {
      outputs.push_back({
          {"name", output->wlr()->name},
          {"effect_frames", output->effectFrames()},
          {"eligible", output->effectEligible()},
      });
    }
    return nlohmann::json{{"ok", {{"outputs", std::move(outputs)}}}};
  }
#endif
```
and the table entry `{"effect-frames", "", "count frames drawn for persistent effects per output", false, &IpcCommands::effectFrames, nullptr},` next to `renderer-recover`. `docs/design/README.md` harness-only IPC list: add `effect-frames`: "per-output count of frames scheduled by persistent effects and the number of eligible instances; effect-only frame assertions in `750_effect_border` and `770_effect_screen_cursor`."

Add a unit test in `tests/unit/ipc_commands.cpp` if that file enumerates the command table (it checks names/arg specs): extend its expected list with `effect-frames` under `UMBRIEL_TEST_IPC`.

- [ ] **Step 3: Run tests**

Run: `nix develop . --command bash -c 'just test && just check 0 60'`
Expected: unit tests pass (including `ipc-commands`), session/IPC checks (`0xx`) and output checks (`60x`) pass.

- [ ] **Step 4: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(output): effect-only frames capped by effects.max_fps"
```

---

### Task 4.6: Harness checks 750, 751, 780

**Files:**
- Create: `tests/harness/checks/750_effect_border.sh`, `tests/harness/checks/751_effect_border_transform.sh`, `tests/harness/checks/780_effect_reload.sh`

- [ ] **Step 1: `750_effect_border.sh`**

```bash
#!/usr/bin/env bash
# A border preset paints the focused window's ring and its padding, leaves the client hole alone, follows focus, can be
# switched off per window, freezes with the animation clock, spills light onto a neighbour, and asks for frames only
# while its clock advances.
set -euo pipefail

readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-border.png"
cat > "$UMBRIEL_RUNTIME_DIR/ring.glsl" <<'GLSL'
// Solid red over the whole drawn rectangle; the hole is cut out by the compositor. The green term is invisible at
// 8 bits but keeps umbriel_time an active uniform, so the program counts as time-reading.
vec4 border(vec2 uv) { return vec4(1.0, 0.001 * sin(umbriel_time), 0.0, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/still.glsl" <<'GLSL'
vec4 border(vec2 uv) { return vec4(1.0, 0.0, 0.0, 1.0); }
GLSL
readonly BASE="$UMBRIEL_RUNTIME_DIR/effect-border-base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[effects]
border = "ring"
[effects.preset.ring]
kind = "border"
shader = "ring.glsl"
padding = 20
[[window_rule]]
match.title = "^effect-(one|two|plain)$"
default_floating = true
[[window_rule]]
match.title = "^effect-plain$"
border_effect = "off"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" "$1" 300 200 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]]
}
red_at() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r > 0.9 && g < 0.1 && b < 0.1' "$1"; }

spawn effect-one
"$UMBRIEL" settle > /dev/null
read -r x y w h id < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
grim "$IMAGE"
# The ring (4 px) plus padding (20 px) paints red; sample a 2x2 patch inside the padding, 12 px above the client.
if (( $(red_at "2x2+$((x + w / 2))+$((y - 12))") < 4 )); then
  echo "border effect did not paint the padding above the focused window"
  exit 1
fi
# The client hole shows the blue client, not the effect.
if (( $("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'b > 0.9 && r < 0.1' "2x2+$((x + w / 2))+$((y + h / 2))") < 4 )); then
  echo "border effect leaked into the client hole"
  exit 1
fi

# Focus moves the effect: the first window loses it, the second gains it.
spawn effect-two
"$UMBRIEL" settle > /dev/null
read -r x2 y2 w2 _ id2 < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
grim "$IMAGE"
if (( $(red_at "2x2+$((x2 + w2 / 2))+$((y2 - 12))") < 4 )); then
  echo "border effect did not follow focus to the second window"
  exit 1
fi
if (( $(red_at "2x2+$((x + w / 2))+$((y - 12))") > 0 )); then
  echo "border effect stayed on the unfocused window"
  exit 1
fi

# border_effect = "off" on a rule keeps the plain ring.
spawn effect-plain
"$UMBRIEL" settle > /dev/null
read -r x3 y3 w3 _ _ < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
grim "$IMAGE"
if (( $(red_at "2x2+$((x3 + w3 / 2))+$((y3 - 12))") > 0 )); then
  echo "border_effect = off did not disable the default on the plain window"
  exit 1
fi
"$UMBRIEL" msg "window-focus:$id2" > /dev/null
"$UMBRIEL" settle > /dev/null

# Frames: a time-reading program requests effect-only frames while the clock advances, none once frozen.
frames() { "$UMBRIEL" effect-frames --json | jq -r '.outputs[0].effect_frames'; }
before=$(frames)
sleep 0.3 # real time: effect-only frames arrive on the output's own timer
if (( $(frames) <= before )); then
  echo "an advancing time-reading border effect requested no effect-only frames"
  exit 1
fi
"$UMBRIEL" clock-freeze
"$UMBRIEL" settle > /dev/null
before=$(frames)
sleep 0.3 # real time: a frozen clock must produce no effect-only frames
if (( $(frames) != before )); then
  echo "a frozen clock still produced effect-only frames: $before -> $(frames)"
  exit 1
fi
"$UMBRIEL" clock-resume
before=$(frames)
sleep 0.3 # real time: resuming the clock restarts effect-only frames
if (( $(frames) <= before )); then
  echo "resuming the clock did not restart effect-only frames"
  exit 1
fi

# animated = false and speed = 0 each stop frames while the clock runs.
for variant in 'animated = false' 'speed = 0'; do
  cat "$BASE" > "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<EOF

[animation]
enabled = false
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects]
border = "ring"
[effects.preset.ring]
kind = "border"
shader = "ring.glsl"
$variant
EOF
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" settle > /dev/null
  before=$(frames)
  sleep 0.3 # real time: a stopped clock must produce no effect-only frames
  if (( $(frames) != before )); then
    echo "$variant still produced effect-only frames"
    exit 1
  fi
done

# Light: a preset with light spills red past its padding over a neighbouring window.
cat "$BASE" > "$UMBRIEL_CONFIG"
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[effects]
border = "lit"
[effects.preset.lit]
kind = "border"
shader = "still.glsl"
padding = 0
[effects.preset.lit.light]
spread = 40
intensity = 4
threshold = 0.2
[[window_rule]]
match.title = "^effect-(one|two|plain)$"
default_floating = true
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" msg "window-focus:$id" > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r r _ _ < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y - 20))")
if (( r < 15 )); then
  echo "border light did not spill above the focused window: red=$r"
  exit 1
fi
echo "border effect padding, hole, focus, off override, frame gating, and light verified"
```

Adjust sample offsets if the harness reports the window at a position where `y - 20` leaves the output; floating windows open centred at 1280x720 so both offsets stay on screen.

- [ ] **Step 2: `751_effect_border_transform.sh`**

```bash
#!/usr/bin/env bash
# harness: outputs=1
# Border uv and umbriel_border_distance are logical on a rotated, fractionally scaled output: the left half of the ring
# (logical x) paints red and the right half blue, and the distance reads zero along the client edge.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-border-transform.png"
cat > "$UMBRIEL_RUNTIME_DIR/halves.glsl" <<'GLSL'
vec4 border(vec2 uv) {
  float d = umbriel_border_distance(uv);
  // Green marks the first two logical pixels outside the client edge; red/blue halves elsewhere.
  if (d >= 0.0 && d < 2.0) return vec4(0.0, 1.0, 0.0, 1.0);
  return uv.x < 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[output."HEADLESS-1"]
scale = 1.25
transform = "90"
[animation]
enabled = false
[appearance]
border_width = 12
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[effects]
border = "halves"
[effects.preset.halves]
kind = "border"
shader = "halves.glsl"
[[window_rule]]
match.title = "^transform$"
default_floating = true
EOF
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFFFFFFFF "$UMBRIEL_UNMAP_CLIENT" transform 300 200 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "transform")')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]]
"$UMBRIEL" settle > /dev/null
grim -s 1 "$IMAGE"
# The screenshot is in logical orientation. Sample the ring 6 px left of the client's left edge (red half) and 6 px
# right of its right edge (blue half), and the first pixel outside the top edge (green).
read -r x y w h < <(jq -r '"\(.x) \(.y) \(.w) \(.h)"' <<< "$window")
red=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r > 0.9 && b < 0.1 && g < 0.1' "2x2+$((x - 8))+$((y + h / 2))")
blue=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'b > 0.9 && r < 0.1 && g < 0.1' "2x2+$((x + w + 6))+$((y + h / 2))")
green=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'g > 0.9 && r < 0.1 && b < 0.1' "2x1+$((x + w / 2))+$((y - 1))")
if (( red < 4 || blue < 4 )); then
  echo "border uv was not logical on the rotated fractional output: red=$red blue=$blue"
  exit 1
fi
if (( green < 1 )); then
  echo "umbriel_border_distance did not read zero at the client edge: green=$green"
  exit 1
fi
echo "border uv halves and border distance survived rotation and fractional scale"
```

- [ ] **Step 3: `780_effect_reload.sh`**

```bash
#!/usr/bin/env bash
# Reload behaviour of effects: a missing shader renders plainly and recovers once the file appears; unknown and
# mismatched names are reported and leave the setting off; a [colors] change reaches a palette shader without a
# recompile.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-reload.png"
readonly LOG_MARK=$(($(wc -l < "$UMBRIEL_LOG") + 1))
cat > "$UMBRIEL_RUNTIME_DIR/palette.glsl" <<'GLSL'
vec4 border(vec2 uv) { return umbriel_palette_at(0.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
accent_primary = "#00FF00FF"
[colors.border]
focused = "#FFFFFFFF"
[effects]
border = "later"
window = "later"
screen = "nope"
[effects.preset.later]
kind = "border"
shader = "later.glsl"
[[window_rule]]
match.title = "^reload$"
default_floating = true
EOF
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 50); do
  grep -q "config reloaded" <(tail -n +"$LOG_MARK" "$UMBRIEL_LOG") && break
  sleep 0.02
done
if ! tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "ignoring effects.window (effect 'later' is a border preset, not a window preset)"; then
  echo "a mismatched preset kind was not reported"
  exit 1
fi
if ! tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "ignoring effects.screen (unknown effect 'nope')"; then
  echo "an unknown preset name was not reported"
  exit 1
fi
if ! tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "cannot read shader file"; then
  echo "the missing shader file was not reported"
  exit 1
fi
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" reload 300 200 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "reload")')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]]
"$UMBRIEL" settle > /dev/null
read -r x y w _ < <(jq -r '"\(.x) \(.y) \(.w) \(.h)"' <<< "$window")
probe() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y - 3))"; }
grim "$IMAGE"
read -r r g b < <(probe)
if (( r < 240 || g < 240 || b < 240 )); then
  echo "an inert preset did not leave the plain white ring: $r $g $b"
  exit 1
fi
# The missing file appears: the watcher reloads and the ring turns accent_primary green.
cp "$UMBRIEL_RUNTIME_DIR/palette.glsl" "$UMBRIEL_RUNTIME_DIR/later.glsl"
sed -i 's/^shader = "later.glsl"$/shader = "later.glsl"\npalette = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r r g b < <(probe)
if (( g < 240 || r > 15 || b > 15 )); then
  echo "the preset did not recover once its shader file appeared: $r $g $b"
  exit 1
fi
# A [colors] change updates the palette uniform without recompiling.
compiles=$(tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -c "Compiling border shader" || true)
sed -i 's/^accent_primary = "#00FF00FF"$/accent_primary = "#0000FFFF"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r r g b < <(probe)
if (( b < 240 || g > 15 )); then
  echo "a colour change did not reach the palette uniform: $r $g $b"
  exit 1
fi
if (( $(tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -c "Compiling border shader" || true) != compiles )); then
  echo "a colour change recompiled the preset"
  exit 1
fi
echo "inert preset recovery, reference diagnostics, and palette updates without recompilation verified"
```
`Compiling border shader` is the `WLR_DEBUG` line from `effect_shader.c`; the harness runs with debug logging (check `tests/harness/check.sh` for `WLR_LOG`/`--log` — if it does not, drop the compile-count assertion and keep the palette pixel assertion).

- [ ] **Step 4: Run the three checks under stress**

Run: `nix develop . --command bash -c 'just check 750 751 780 && just check-stress 750 8 && just check-stress 751 8 && just check-stress 780 8'`
Expected: all pass. Tune probe offsets against actual window placement (`umbriel windows --json`) if a sample lands off the ring.

- [ ] **Step 5: Stage gate and commit**

```bash
nix develop . --command bash -c 'just format && git diff --exit-code && just lint && just test && just check'
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): border effect, transform, and reload checks"
```
Expected: clean.
