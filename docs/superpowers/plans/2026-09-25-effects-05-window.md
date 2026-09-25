# Effects Stage 5: Window effects, in-place composition, capture policy

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `window` preset (default or `window_effect`) shades every matching window in place — reading the output framebuffer under the window at rest, or the enclosing capture target inside an animation — through the rounded mask; a border preset's `overlay` rides the same path on the surface node while the border effect applies; `in_capture = false` feeds screencasts an unfiltered composition with isolated feedback history.

**Architecture:** umbrielfx gains the in-place slot mode: the subtree renders normally into the current target, the pass copies the target region under the node into an offscreen source, and the program writes back with blending off through the mask suffix. Composition roles (display vs. unfiltered capture) key feedback history. When a capture is pending and `in_capture` is off, `build_state` composes twice: an unfiltered pass saved into an `effect_capture` buffer that dmabuf imports read, then the display pass. In the compositor, `ViewEffects` binds the window and overlay slots on the view's surface tree node, the capture scene's node when `in_capture` is on, the close snapshot's content tree, and overview card buffers.

**Tech Stack:** C23 umbrielfx, C++23, harness with `grim` (screencopy) for capture assertions.

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §3 (Slots: In place; Window sampling contract; State, scanout, capture: Capture policy), §4 (window), §6 (760, the 770 feedback extension lands in Stage 6). Index: `2026-09-25-effects-00-index.md`.

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Reuse the common effect compositor, `fx_render_pass_read_to_buffer`, framebuffer allocation/ownership, history promotion, and the extracted render fixture. Share drawn-bounds and damage-expansion logic across node and output effects; keep capture role and output identity explicit.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- In-place slots never begin a capture; they run after the node's subtree has rendered into whatever the current target is.
- An unfiltered pass never reads, writes, or promotes display history; each role promotes only after successful submission, once per frame.
- The `effect_capture` buffer is only allocated while a capture is pending on that output with `in_capture = false`; it is released when no capture is pending.
- Window effects apply regardless of focus, decoration, or fullscreen.

---

### Task 5.1: In-place composition in the render pass

**Files:**
- Modify: `umbrielfx/internal/render/fx_renderer/effect.h` (`fx_effect_composite` gains `bool replace; unsigned role; const float* corner_radius;`), `umbrielfx/render/fx_renderer/fx_pass.c` (`fx_render_pass_end_effect` refactor, new `fx_render_pass_effect_in_place`, history role), `umbrielfx/include/umbrielfx/render/fx_renderer/fx_offscreen_buffers.h` (`struct fx_framebuffer *in_place_source;`), `umbrielfx/render/fx_renderer/fx_offscreen_buffers.c` (`clear_effect_buffers` drops it), `umbrielfx/internal/render/fx_renderer/animation_history.h`
- Test: `umbrielfx/tests/effects.c` (`in-place` case)

**Interfaces:**
- Produces (internal `effect.h`):
  ```c
  // Renders `composite->shader` over the current target's pixels under `box`,
  // writing back with blending off through the rounded mask. The subtree must
  // already be drawn. Reads and promotes history like a capture composite.
  void fx_render_pass_effect_in_place(struct fx_gles_render_pass* pass, const struct fx_effect_composite* composite);
  ```
  `fx_effect_composite` gains: `bool replace;` (write without blending; set by in-place), `unsigned role;` (0 display, 1 unfiltered capture — selects the history), `const float* corner_radius;` (4 logical radii tl,tr,br,bl for `umbriel_corner_radius`, may be NULL).
- Produces (`animation_history.h`): `void fx_animation_history_reset_role(struct fx_animation_history* history, struct wlr_output* output, unsigned role);` — destroys the entries of that role on that output (NULL output: every output).

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
// An in-place window program reads what is already on the target (the blue
// background through a translucent client) and rewrites only its own
// rectangle, keeping the corner fringe.
static bool test_in_place(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 }, half_red[4] = { 0.5f, 0, 0, 0.5f };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct wlr_scene_rect *window = wlr_scene_rect_create(&scene->tree, 8, 8, half_red);
	wlr_scene_node_set_position(&window->node, 4, 4);
	wlr_scene_rect_set_corner_radius(window, 3);
	// Swap red and blue of whatever is under the window: 0.5 red over blue becomes 0.5 blue over red.
	struct fx_effect_shader *program = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return umbriel_sample(uv).bgra; }", "in-place");
	bool ok = check(program != NULL, "window program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&window->node, FX_SLOT_WINDOW, program, &parameters);
	struct wlr_output_state state;
	struct wlr_buffer *rendered = fixture_render_scene(fixture, scene_output, &state);
	ok &= check(rendered != NULL, "renders");
	if (rendered != NULL) {
		uint8_t centre[4], corner[4], outside[4];
		ok &= fixture_read_pixel(fixture, rendered, 8, 8, centre);
		ok &= fixture_read_pixel(fixture, rendered, 4, 4, corner);
		ok &= fixture_read_pixel(fixture, rendered, 2, 2, outside);
		// Under the window: 0.5 red + 0.5 blue -> swapped: red 0.5, blue 0.5 (the backdrop was seen and rewritten).
		ok &= check(centre[2] > 100 && centre[2] < 160 && centre[0] > 100 && centre[0] < 160, "the program read the backdrop through the translucent window");
		ok &= check(corner[0] > 250 && corner[2] < 5, "the rounded corner keeps the untouched background");
		ok &= check(outside[0] > 250 && outside[2] < 5, "nothing outside the window changes");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	fx_effect_shader_unref(program);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```
Register `in-place`.

Add a second case that proves partial damage cannot leave stale effect output (the whole-box invalidation from Stage 1 Task 1.4):

```c
// A mirror program reads the far side of its rectangle. After a small change
// on one side, the mirrored pixels on the other side must update too, even
// though only the small area was damaged.
static bool test_damage_expansion(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 }, red[4] = { 1, 0, 0, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, black);
	// The effect node: a 12x12 tree at (2,2) holding a white background and a 2x2 marker that moves.
	struct wlr_scene_tree *window = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_set_position(&window->node, 2, 2);
	wlr_scene_rect_create(window, 12, 12, white);
	struct wlr_scene_rect *marker = wlr_scene_rect_create(window, 2, 2, red);
	wlr_scene_node_set_position(&marker->node, 0, 5);   // left edge, middle rows
	struct fx_effect_shader *mirror = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return umbriel_sample(vec2(1.0 - uv.x, uv.y)); }", "mirror");
	bool ok = check(mirror != NULL, "mirror program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&window->node, FX_SLOT_WINDOW, mirror, &parameters);
	const struct wlr_drm_format *format = get_render_format(fixture, DRM_FORMAT_ARGB8888);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, TEST_WIDTH, TEST_HEIGHT, format);
	// Warm-up frames with whole damage, each acknowledged like a commit, until the swapchain hands back a buffer
	// it has rendered before. Then the pending damage is empty and frame 2 starts from partial damage only.
	struct wlr_output_state state;
	struct wlr_scene_output_state_options options = { .swapchain = swapchain };
	uint8_t pixel[4];
	for (int warm = 0; warm < 4; warm++) {
		wlr_output_state_init(&state);
		wlr_scene_output_damage_whole_for_test(scene_output);
		ok &= check(wlr_scene_output_build_state(scene_output, &state, &options) && state.buffer != NULL, "warm-up frame");
		if (state.buffer != NULL) {
			ok &= fixture_read_pixel(fixture, state.buffer, 12, 8, pixel);
			ok &= check(pixel[2] > 250 && pixel[1] < 5, "a whole-damage frame mirrors the marker to the right edge");
		}
		wlr_scene_output_acknowledge_damage_for_test(scene_output, &state);
		wlr_output_state_finish(&state);
	}
	ok &= check(!wlr_scene_output_needs_frame(scene_output), "acknowledged frames leave no pending damage");
	// Frame 2: move the marker down by 4 rows. Only the two small rects are damaged by the scene; the mirrored copy at
	// the right edge lies outside that damage and must still update.
	wlr_scene_node_set_position(&marker->node, 0, 9);
	ok &= check(wlr_scene_output_needs_frame(scene_output), "the move produced partial damage");
	wlr_output_state_init(&state);
	ok &= check(wlr_scene_output_build_state(scene_output, &state, &options) && state.buffer != NULL, "frame 2");
	if (state.buffer != NULL) {
		uint8_t old_spot[4], new_spot[4];
		ok &= fixture_read_pixel(fixture, state.buffer, 12, 8, old_spot);
		ok &= fixture_read_pixel(fixture, state.buffer, 12, 12, new_spot);
		ok &= check(old_spot[2] > 250 && old_spot[1] > 250, "the stale mirrored marker was repainted white");
		ok &= check(new_spot[2] > 250 && new_spot[1] < 5, "the moved marker is mirrored at its new rows");
	}
	wlr_scene_output_acknowledge_damage_for_test(scene_output, &state);
	wlr_output_state_finish(&state);
	wlr_swapchain_destroy(swapchain);
	fx_effect_shader_unref(mirror);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```
Two test-only hooks, declared in `effect.h` under one comment saying they exist for `tests/effects.c`, defined next to `wlr_scene_output_set_effect_capture_policy`:

- `void wlr_scene_output_damage_whole_for_test(struct wlr_scene_output*)` calls `scene_output_damage_whole`.
- `void wlr_scene_output_acknowledge_damage_for_test(struct wlr_scene_output* output, const struct wlr_output_state* state)` performs exactly what `scene_output_handle_commit` (`wlr_scene.c:3176-3188`) does for a committed buffer: subtracts `state->damage` from `pending_commit_damage` (or clears it when the state carries no damage). Without it, `build_state` alone never clears `pending_commit_damage` (only a commit does), so the whole-output warm-up damage would keep feeding the commit-side expansion and the test could pass without the render-side expansion.

The four warm-up frames cover the swapchain's default capacity so frame 2 lands on an already-rendered buffer; `wlr_damage_ring_rotate_buffer` gives it only the damage since its last use. Register `damage-expansion`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-in-place --print-errorlogs'`
Expected: FAIL — the window slot currently runs as a capture composite: the program sees only the translucent window's own pixels, so the centre comes out half red, not swapped.

- [ ] **Step 3: Refactor `fx_render_pass_end_effect` and add the in-place path**

In `fx_pass.c`:

1. Extract the body of `fx_render_pass_end_effect` after `pop_animation_capture` into `static void effect_composite(struct fx_gles_render_pass* pass, const struct fx_effect_composite* composite, struct wlr_texture* texture, const struct wlr_box* source_box)`, where `texture` is the input and `source_box` the region of it that maps to the drawn box (for a capture it is the drawn box itself, since capture targets are pass-target sized). `fx_render_pass_end_effect` becomes: pop, expand boxes, `effect_composite(pass, composite, texture, &drawn)`, destroy texture.
2. Inside `effect_composite`:
   - `animation_history_get_output(history, output, renderer, role, create)`: add `unsigned role` to `struct fx_animation_output_history` and to the lookup (`output_history->output == output && output_history->role == role`). `fx_animation_history_reset_role` finishes only entries of that role.
   - The final `fx_render_pass_add_texture` of the history result uses `.blend_mode = composite->replace ? WLR_RENDER_BLEND_MODE_NONE : WLR_RENDER_BLEND_MODE_PREMULTIPLIED`; the fallback `draw_animation_texture` call passes `blend = !composite->replace`.
   - `draw_animation_texture` gains `const float* corner_radius`: when non-NULL bind `umbriel_corner_radius` (VEC4) through `fx_effect_shader_bind_uniform`.
3. Add:
```c
void fx_render_pass_effect_in_place(struct fx_gles_render_pass* pass, const struct fx_effect_composite* composite) {
  if (pass->fx_offscreen_buffers == NULL) {
    return;
  }
  struct wlr_box drawn = composite->box;
  struct wlr_box logical = composite->logical_box;
  expand_animation_boxes(&drawn, &logical, composite->expand);
  struct wlr_box target_box = {.width = pass->buffer->buffer->width, .height = pass->buffer->buffer->height};
  struct wlr_box visible;
  if (!wlr_box_intersection(&visible, &drawn, &target_box)) {
    return;
  }
  // Sampled from a copy: a program may read any texel of its rectangle while
  // writing others, which GL forbids on the bound target.
  struct fx_framebuffer* source = ensure_offscreen_buffer(pass, &pass->fx_offscreen_buffers->in_place_source, true);
  if (source == NULL) {
    return;
  }
  pixman_region32_t region;
  pixman_region32_init_rect(&region, visible.x, visible.y, visible.width, visible.height);
  fx_render_pass_read_to_buffer(pass, &region, source, pass->buffer);
  pixman_region32_fini(&region);
  struct wlr_texture* texture = fx_texture_from_buffer(&pass->buffer->renderer->wlr_renderer, source->buffer);
  if (texture == NULL || fx_get_texture(texture)->target != GL_TEXTURE_2D) {
    if (texture != NULL) {
      wlr_texture_destroy(texture);
    }
    fx_framebuffer_bind(pass->buffer);
    return;
  }
  struct fx_effect_composite replace = *composite;
  replace.replace = true;
  replace.box = drawn;
  replace.logical_box = logical;
  replace.expand = 0;   // already applied
  effect_composite(pass, &replace, texture, &drawn);
  wlr_texture_destroy(texture);
}
```
`fx_render_pass_read_to_buffer` copies with `WLR_RENDER_BLEND_MODE_NONE` and EXT_LINEAR under a colour transform (`fx_pass.c:2305`), which is exactly the untouched-pixel copy needed here. Add `struct fx_framebuffer *in_place_source;` to `fx_offscreen_buffers` (after `effects_buffer_swapped`) and drop it in `clear_effect_buffers`.

- [ ] **Step 4: Scene side: run in-place slots after the subtree**

In `render_animated_range` (`wlr_scene.c`):

- The capture-begin loop skips in-place slots:
  ```c
      if (shader != NULL && shader->renderer == pass->buffer->renderer && !fx_slot_in_place(slot)) {
  ```
  and `captured_any` reflects only capture slots.
- Right after `render_animated_range(entries, i, end, animation->node, data);` (the subtree render) and the bounds/box computation, before the capture composites, run:
  ```c
    if (!data->effect_capture) {
      float corners[4];
      const float* corner_radius = in_place_corners(animation->node, corners) ? corners : NULL;
      for (unsigned slot = 0; slot <= FX_SLOT_OVERLAY; slot++) {
        struct fx_effect_shader* shader = animation->shaders[slot];
        if (shader == NULL || shader->renderer != pass->buffer->renderer) {
          continue;
        }
        const struct fx_effect_composite composite = {
            .shader = shader,
            .parameters = &animation->parameters[slot],
            .box = box,
            .logical_box = logical_box,
            .transform = data->transform,
            .expand = 0,
            .capture_clip = &clip,
            .output_clip = &clip,
            .history = &animation->histories[slot],
            .output = data->output->output,
            .update_history = !data->shadow_capture,
            .role = 0,
            .corner_radius = corner_radius,
        };
        fx_render_pass_effect_in_place(pass, &composite);
      }
    }
  ```
  (`data->effect_capture` is added in Task 5.2; declare it in `render_data` now, always false.)
- Add the helper:
  ```c
  // Corner radii the in-place mask uses: a buffer's own, or a tree's first enabled buffer (the toplevel surface).
  static bool in_place_corners(struct wlr_scene_node* node, float out[4]) {
    struct wlr_scene_buffer* buffer = NULL;
    if (node->type == WLR_SCENE_NODE_BUFFER) {
      buffer = wlr_scene_buffer_from_node(node);
    } else if (node->type == WLR_SCENE_NODE_TREE) {
      struct wlr_scene_node* child;
      wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
        if (child->enabled && child->type == WLR_SCENE_NODE_BUFFER) {
          buffer = wlr_scene_buffer_from_node(child);
          break;
        }
      }
    } else if (node->type == WLR_SCENE_NODE_RECT) {
      struct wlr_scene_rect* rect = wlr_scene_rect_from_node(node);
      out[0] = rect->corners.top_left; out[1] = rect->corners.top_right;
      out[2] = rect->corners.bottom_right; out[3] = rect->corners.bottom_left;
      return true;
    }
    if (buffer == NULL) {
      return false;
    }
    out[0] = buffer->corners.top_left;
    out[1] = buffer->corners.top_right;
    out[2] = buffer->corners.bottom_right;
    out[3] = buffer->corners.bottom_left;
    return true;
  }
  ```
- The `outer_animation` walk already returns nodes with in-place-only slots; with `captured_any == false` and no clip the existing fallback path is skipped and the subtree renders straight into the target — which is what in-place needs. Make sure the `if (!captured_any && has_animation_clip)` fallback still runs its in-place slots: restructure so the in-place block sits in a small static function `render_in_place_slots(animation, data, &box, &logical_box, &clip)` called from both paths.
- `scene_node_opaque_region` already exempts nodes under any effect (Stage 1), so the backdrop beneath a translucent window stays drawn for the in-place program to read.

- [ ] **Step 5: Run tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs && just build && just check 18 19 20 750 751'`
Expected: `effects-in-place` passes (centre swapped, corner and outside untouched) and `effects-damage-expansion` passes (the stale mirrored marker is repainted); every earlier case and check passes. If `damage-expansion` fails on "repainted white", the Stage 1 whole-box invalidation is not reaching `render_data.damage` — check both call sites in `build_state`.

- [ ] **Step 6: Commit**

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): in-place window and overlay slots"
```

---

### Task 5.2: Capture policy: unfiltered composition and role-keyed history

**Files:**
- Modify: `umbrielfx/include/umbrielfx/render/effect.h` (`wlr_scene_output_set_effect_capture_policy`), `umbrielfx/include/umbrielfx/types/wlr_scene.h` (`wlr_scene_output_state_options` gains `bool effect_capture_pending;`), `umbrielfx/internal/render/fx_renderer/fx_renderer.h` (`struct fx_framebuffer` gains `effect_capture_buffer`, `effect_capture_parent`, `effect_capture_valid`, `effect_capture_owner`), `umbrielfx/include/umbrielfx/render/pass.h` (`fx_gles_render_pass` gains `bool effect_capture_saved;`), `umbrielfx/render/fx_renderer/fx_pass.c` (`fx_render_pass_save_effect_capture`, submit hook, begin hook), `umbrielfx/render/fx_renderer/fx_texture.c:524-545`, `umbrielfx/render/fx_renderer/fx_framebuffer.c` (`fx_framebuffer_destroy`), `umbrielfx/types/scene/wlr_scene.c` (`scene_output_effects` addon, `build_state`, `scene_entry_render` output_sample)
- Test: `umbrielfx/tests/effects.c` (`capture-policy` case)

**Interfaces:**
- Produces (public): `void wlr_scene_output_set_effect_capture_policy(struct wlr_scene_output* output, bool in_capture);` and `wlr_scene_output_state_options.effect_capture_pending`.
- Produces (internal): `bool fx_render_pass_save_effect_capture(struct fx_gles_render_pass* pass);` — copies the current (unfiltered) target into the output buffer's `effect_capture_buffer`.
- Produces (static in `wlr_scene.c`): `struct scene_output_effects { struct wlr_addon addon; struct wlr_scene_output* output; struct wl_listener destroy; bool in_capture; bool capture_was_pending; /* stage 6: screen, cursor */ }`; `scene_output_effects_get(scene_output, create)`; `render_data.effect_capture` (bool: this composition is the unfiltered one).

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
// With in_capture off and a capture pending, a dmabuf import of the rendered
// buffer sees the unfiltered composition while the display keeps the effect.
static bool test_capture_policy(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 }, red[4] = { 1, 0, 0, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct wlr_scene_rect *window = wlr_scene_rect_create(&scene->tree, 8, 8, red);
	wlr_scene_node_set_position(&window->node, 4, 4);
	struct fx_effect_shader *program = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }", "capture-policy");
	bool ok = check(program != NULL, "window program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&window->node, FX_SLOT_WINDOW, program, &parameters);
	wlr_scene_output_set_effect_capture_policy(scene_output, false);

	const struct wlr_drm_format *format = get_render_format(fixture, DRM_FORMAT_ARGB8888);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, TEST_WIDTH, TEST_HEIGHT, format);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	struct wlr_scene_output_state_options options = { .swapchain = swapchain, .effect_capture_pending = true };
	ok &= check(wlr_scene_output_build_state(scene_output, &state, &options) && state.buffer != NULL, "renders with a pending capture");
	if (ok) {
		uint8_t display[4], captured[4];
		// The swapchain buffer holds the display composition. Read it through its framebuffer: a texture import
		// of the same buffer is exactly what the capture policy redirects.
		ok &= fixture_read_display_pixel(fixture, state.buffer, 8, 8, display);
		ok &= check(display[1] > 250 && display[2] < 5, "the display shows the window effect");
		// A dmabuf import (what screencopy and image-copy do) resolves to the unfiltered capture.
		struct wlr_texture *import = wlr_texture_from_buffer(fixture->renderer, state.buffer);
		uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * 4];
		ok &= check(import != NULL && wlr_texture_read_pixels(import, &(struct wlr_texture_read_pixels_options) {
			.data = pixels, .format = DRM_FORMAT_ARGB8888, .stride = TEST_WIDTH * 4 }), "import reads");
		memcpy(captured, &pixels[(8 * TEST_WIDTH + 8) * 4], 4);
		ok &= check(captured[2] > 250 && captured[1] < 5, "the capture sees the plain red window");
		wlr_texture_destroy(import);
	}
	wlr_output_state_finish(&state);
	// in_capture = true: the import sees the effect too.
	wlr_scene_output_set_effect_capture_policy(scene_output, true);
	wlr_output_state_init(&state);
	ok &= check(wlr_scene_output_build_state(scene_output, &state, &options) && state.buffer != NULL, "renders again");
	if (state.buffer != NULL) {
		struct wlr_texture *import = wlr_texture_from_buffer(fixture->renderer, state.buffer);
		uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * 4];
		ok &= check(import != NULL && wlr_texture_read_pixels(import, &(struct wlr_texture_read_pixels_options) {
			.data = pixels, .format = DRM_FORMAT_ARGB8888, .stride = TEST_WIDTH * 4 }), "import reads");
		ok &= check(pixels[(8 * TEST_WIDTH + 8) * 4 + 1] > 250, "with in_capture the capture includes the effect");
		wlr_texture_destroy(import);
	}
	wlr_output_state_finish(&state);
	wlr_swapchain_destroy(swapchain);
	fx_effect_shader_unref(program);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```
The capture read relies on `read_buffer`'s `wlr_texture_from_buffer` path being the dmabuf import path (`fx_texture_from_dmabuf` at `fx_texture.c:524`), which is where the capture buffer substitutes — the same path screencopy takes. The display read must therefore bypass imports. Add to `render_fixture.h`:

```c
// Reads one pixel of the buffer's own framebuffer with glReadPixels. Unlike
// read_buffer this never imports the buffer as a texture, so it sees the
// display composition even while a capture substitute is valid.
static bool fixture_read_display_pixel(struct fixture *fixture, struct wlr_buffer *buffer, int x, int y, uint8_t out[4]) {
	struct fx_renderer *renderer = fx_get_renderer(fixture->renderer);
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(renderer->egl, &previous)) {
		return false;
	}
	GLuint fbo = fx_renderer_get_buffer_fbo(fixture->renderer, buffer);
	bool ok = fbo != 0;
	if (ok) {
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		uint8_t rgba[4];
		// GL reads bottom-up; the pass projects with FLIPPED_180, so row y is TEST_HEIGHT - 1 - y.
		glReadPixels(x, TEST_HEIGHT - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		ok = glGetError() == GL_NO_ERROR;
		out[0] = rgba[2]; out[1] = rgba[1]; out[2] = rgba[0]; out[3] = rgba[3];   // B G R A like fixture_read_pixel
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	wlr_egl_restore_context(&previous);
	return ok;
}
```
(`render/egl.h` and `render/fx_renderer/fx_renderer.h` are already included by the fixture; if the row orientation comes out inverted on the first run, flip the `y` expression — the assertion on a centre pixel is symmetric, so use a corner pixel from `test_in_place` to settle it once.) Register `capture-policy`.

Add a second case for history isolation and the first-frame fallback:

```c
// Feedback history is keyed by composition role: the unfiltered capture pass
// never reads or advances the display's history, and a role without history
// yet reads its own current input.
static bool test_capture_feedback(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float black[4] = { 0, 0, 0, 1 }, blue[4] = { 0, 0, 1, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, black);
	struct wlr_scene_rect *window = wlr_scene_rect_create(&scene->tree, 8, 8, blue);
	wlr_scene_node_set_position(&window->node, 4, 4);
	// Each frame adds 0.25 red to the previous result; the first frame sees its input (blue, red 0).
	struct fx_effect_shader *accumulate = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { vec4 p = umbriel_sample_previous(uv); return vec4(min(p.r + 0.25, 1.0), 0.0, umbriel_sample(uv).b, 1.0); }", "accumulate");
	struct fx_effect_shader *green = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }", "green");
	bool ok = check(accumulate != NULL && green != NULL, "programs compile");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1, .transition_id = 7 };
	wlr_scene_node_set_animation(&window->node, FX_SLOT_WINDOW, green, &parameters);
	wlr_scene_node_set_animation(&window->node, FX_SLOT_WINDOWS_IN, accumulate, &parameters);
	wlr_scene_output_set_effect_capture_policy(scene_output, false);
	const struct wlr_drm_format *format = get_render_format(fixture, DRM_FORMAT_ARGB8888);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, TEST_WIDTH, TEST_HEIGHT, format);
	uint8_t display[4], captured[4];
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * 4];
	for (int frame = 0; frame < 3; frame++) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		// Frames 0 and 1 have a capture pending; frame 2 does not.
		struct wlr_scene_output_state_options options = { .swapchain = swapchain, .effect_capture_pending = frame < 2 };
		wlr_scene_output_damage_whole_for_test(scene_output);   // see note below
		ok &= check(wlr_scene_output_build_state(scene_output, &state, &options) && state.buffer != NULL, "renders");
		if (!ok) { wlr_output_state_finish(&state); break; }
		ok &= fixture_read_display_pixel(fixture, state.buffer, 8, 8, display);
		if (frame < 2) {
			struct wlr_texture *import = wlr_texture_from_buffer(fixture->renderer, state.buffer);
			ok &= check(import != NULL && wlr_texture_read_pixels(import, &(struct wlr_texture_read_pixels_options) {
				.data = pixels, .format = DRM_FORMAT_ARGB8888, .stride = TEST_WIDTH * 4 }), "import reads");
			memcpy(captured, &pixels[(8 * TEST_WIDTH + 8) * 4], 4);
			wlr_texture_destroy(import);
			// Capture role: first frame red 0.25 (fallback to its own input), second 0.5; blue from the plain client.
			ok &= check(captured[0] > 250, "the capture role sees the plain client");
			ok &= check(captured[2] > 55 && captured[2] < 75 + 64 * frame, "the capture role accumulates on its own");
		}
		// Display role: red grows by 0.25 per frame regardless of captures, and the window is green underneath (blue 0).
		const int expected = 64 * (frame + 1);
		ok &= check(display[2] > expected - 12 && display[2] < expected + 12, "the display role accumulates once per frame");
		ok &= check(display[0] < 5, "the display role never sees the capture's plain client");
		wlr_output_state_finish(&state);
	}
	wlr_swapchain_destroy(swapchain);
	fx_effect_shader_unref(accumulate);
	fx_effect_shader_unref(green);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```
`build_state` renders nothing when the damage ring is empty, so each iteration needs whole damage: add a tiny test-only hook next to `wlr_scene_output_set_effect_capture_policy` — `void wlr_scene_output_damage_whole_for_test(struct wlr_scene_output*)` calling `scene_output_damage_whole` — declared in `effect.h` under a comment that it exists for `tests/effects.c` (the transient `WINDOWS_IN` slot already damages the whole output on every frame in practice, but the fixture has no frame loop). Register `capture-feedback` in `main` and meson. Promotion-after-submit is covered structurally: the history's `valid` flag flips only in `animation_history_commit_updates`, which runs from `render_pass_submit`; there is no way to fail a submit from the fixture, so that path keeps its existing coverage by inspection.

- [ ] **Step 2: Run the test to verify it fails**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-capture-policy --print-errorlogs'`
Expected: compile error (`wlr_scene_output_set_effect_capture_policy`, `effect_capture_pending`).

- [ ] **Step 3: Port the capture buffer**

`internal fx_renderer.h`, `struct fx_framebuffer`: add after `bool sdr_capture_valid;`:
```c
	// Unfiltered composition of this swapchain buffer while a capture is pending
	// with effects excluded from captures. dmabuf imports read it instead.
	struct fx_framebuffer *effect_capture_buffer;
	struct fx_framebuffer *effect_capture_parent;
	bool effect_capture_valid;
	const void *effect_capture_owner;
```
`pass.h`, `fx_gles_render_pass`: add `bool effect_capture_saved;`.

`fx_pass.c`:
```c
bool fx_render_pass_save_effect_capture(struct fx_gles_render_pass* pass) {
  struct fx_framebuffer* output = pass->output_buffer;
  struct fx_renderer* renderer = output->renderer;
  struct fx_framebuffer** capture = &output->effect_capture_buffer;
  if (*capture != NULL && (*capture)->buffer->n_locks > 0) {
    (*capture)->effect_capture_parent = NULL;
    wlr_buffer_drop((*capture)->buffer);
    *capture = NULL;
  }
  struct wlr_allocator* allocator = pass->fx_offscreen_buffers != NULL ? pass->fx_offscreen_buffers->allocator : renderer->allocator;
  bool failed = false, ok = false;
  if (allocator == NULL) {
    return false;
  }
  fx_framebuffer_get_or_create_custom(
      renderer, allocator, output->buffer->width, output->buffer->height, output->drm_format, capture, &failed
  );
  if (failed || *capture == NULL) {
    goto restore;
  }
  (*capture)->effect_capture_parent = output;
  if (!pass->has_color_transform || output->capture_sdr) {
    struct wlr_texture* source = fx_texture_from_buffer(&renderer->wlr_renderer, pass->buffer->buffer);
    if (source == NULL) {
      goto restore;
    }
    struct wlr_render_pass* copy = wlr_renderer_begin_buffer_pass(&renderer->wlr_renderer, (*capture)->buffer, NULL);
    if (copy != NULL) {
      wlr_render_pass_add_texture(copy, &(struct wlr_render_texture_options){
          .texture = source,
          .dst_box = {0, 0, output->buffer->width, output->buffer->height},
          .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
          .filter_mode = WLR_SCALE_FILTER_NEAREST,
          .transfer_function = pass->has_color_transform ? WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR : WLR_COLOR_TRANSFER_FUNCTION_SRGB,
      });
      ok = wlr_render_pass_submit(copy);
    }
    wlr_texture_destroy(source);
  } else {
    // Two-pass HDR: resolve the linear blend buffer through the output transform into the capture.
    pass->output_buffer = *capture;
    ok = render_pass_apply_output_transform(pass);
    pass->output_buffer = output;
  }
  // Valid only once the pass submits.
  pass->effect_capture_saved = ok;
restore:
  fx_framebuffer_bind(pass->buffer);
  glViewport(0, 0, pass->buffer->buffer->width, pass->buffer->buffer->height);
  return ok;
}
```
In `render_pass_submit` (after `animation_history_commit_updates(pass, ok);`):
```c
  pass->output_buffer->effect_capture_valid = ok && pass->effect_capture_saved;
  if (!pass->effect_capture_saved && pass->output_buffer->effect_capture_buffer != NULL) {
    struct fx_framebuffer* capture = pass->output_buffer->effect_capture_buffer;
    capture->effect_capture_parent = NULL;
    pass->output_buffer->effect_capture_buffer = NULL;
    wlr_buffer_drop(capture->buffer);
  }
```
In `fx_begin_buffer_pass`: `buffer->effect_capture_valid = false;`.

`fx_texture.c` `fx_texture_from_dmabuf` (:533): 
```c
	struct fx_framebuffer *texture_buffer = buffer;
	if (buffer->effect_capture_valid && buffer->effect_capture_buffer != NULL) {
		texture_buffer = buffer->effect_capture_buffer;
	} else if (buffer->capture_sdr) {
```
`fx_framebuffer_destroy`: mirror the `sdr_capture_*` unlinking for `effect_capture_buffer`/`effect_capture_parent` (drop the child buffer; clear the parent's pointer and `effect_capture_valid`).

- [ ] **Step 4: The per-output addon and the double composition**

`wlr_scene.h` `wlr_scene_output_state_options`: add after `capture_sdr`:
```c
	/**
	 * A screencopy or image-copy client will read this frame. With effects
	 * excluded from captures, the scene composes an unfiltered frame for it.
	 */
	bool effect_capture_pending;
```

`wlr_scene.c`:
```c
struct scene_output_effects {
  struct wlr_addon addon;
  struct wlr_scene_output* output;
  struct wl_listener destroy;
  bool in_capture;
  bool capture_was_pending;
};

static void scene_output_effects_destroy(struct wlr_addon* addon) {
  struct scene_output_effects* effects = wl_container_of(addon, effects, addon);
  wl_list_remove(&effects->destroy.link);
  wlr_addon_finish(addon);
  free(effects);
}
static const struct wlr_addon_interface scene_output_effects_impl = {.name = "scene_output_effects", .destroy = scene_output_effects_destroy};
static void scene_output_effects_handle_destroy(struct wl_listener* listener, void* data) {
  struct scene_output_effects* effects = wl_container_of(listener, effects, destroy);
  scene_output_effects_destroy(&effects->addon);
}
static struct scene_output_effects* scene_output_effects_get(struct wlr_scene_output* output, bool create) {
  struct wlr_addon* addon = wlr_addon_find(&output->output->addons, output, &scene_output_effects_impl);
  if (addon != NULL) {
    struct scene_output_effects* effects = wl_container_of(addon, effects, addon);
    return effects;
  }
  if (!create) {
    return NULL;
  }
  struct scene_output_effects* effects = calloc(1, sizeof(*effects));
  if (effects == NULL) {
    return NULL;
  }
  effects->output = output;
  wlr_addon_init(&effects->addon, &output->output->addons, output, &scene_output_effects_impl);
  effects->destroy.notify = scene_output_effects_handle_destroy;
  wl_signal_add(&output->events.destroy, &effects->destroy);
  return effects;
}

// Drops the capture-role feedback history of every effect on `output` only.
static void scene_reset_capture_histories(struct wlr_scene* scene, struct wlr_output* output) {
  struct scene_effects* effects = scene_effects_get(scene, false);
  if (effects == NULL) {
    return;
  }
  struct scene_animation* animation;
  wl_list_for_each(animation, &effects->animations, link) {
    for (unsigned slot = 0; slot < FX_ANIMATION_SLOTS; slot++) {
      fx_animation_history_reset_role(&animation->histories[slot], output, 1);
    }
  }
}

void wlr_scene_output_set_effect_capture_policy(struct wlr_scene_output* output, bool in_capture) {
  // The default (false) needs no state: the addon exists only once something is non-default or an
  // output effect is attached, so a configuration without effects allocates nothing here.
  struct scene_output_effects* effects = scene_output_effects_get(output, in_capture);
  if (effects == NULL || effects->in_capture == in_capture) {
    return;
  }
  effects->in_capture = in_capture;
  scene_reset_capture_histories(output->scene, output->output);
  scene_output_damage_whole(output);
}
```
`scene_reset_capture_histories` filters by output as well as role: `fx_animation_history_reset_role(history, output, 1)` destroys only the entries whose `output` and `role` match (a NULL output means every output). Ending a capture on A must leave B's capture-role history alone. Declare it as `void fx_animation_history_reset_role(struct fx_animation_history* history, struct wlr_output* output, unsigned role);` in Task 5.1 and pass the output everywhere.

`render_data`: add `bool effect_capture;` and `unsigned role;` — set `.role = data->effect_capture ? 1 : 0` in every `fx_effect_composite` built in `render_animated_range`, and skip the in-place block when `data->effect_capture`. In `scene_entry_render`, the `output_sample` emission (`:2716`) becomes `if (!data->shadow_capture && !data->effect_capture)`.

`wlr_scene_output_build_state`, after the background rect is drawn and before `render_animated_range(list_data, list_len - 1, 0, NULL, &render_data);`:
```c
  struct scene_output_effects* output_effects = scene_output_effects_get(scene_output, false);
  // Absent state is the default policy: effects are excluded from captures. Bookkeeping is created only
  // when an unfiltered composition actually runs, so an output without effects never gets the addon.
  const bool exclude_from_capture = output_effects == NULL || !output_effects->in_capture;
  const bool capture_pending = options->effect_capture_pending;
  const bool unfiltered_pass = exclude_from_capture && capture_pending && render_data.persistent_visible;
  if (output_effects == NULL && unfiltered_pass) {
    output_effects = scene_output_effects_get(scene_output, true);
  }
  if (output_effects != NULL) {
    if (output_effects->capture_was_pending && !capture_pending) {
      // The capture ended: its histories and buffer go with it.
      scene_reset_capture_histories(scene_output->scene, output);
      output_effects_release_capture(scene_output);
    }
    output_effects->capture_was_pending = capture_pending;
  }
  if (unfiltered_pass && output_effects != NULL) {
    struct render_data clean = render_data;
    clean.effect_capture = true;
    render_animated_range(list_data, list_len - 1, 0, NULL, &clean);
    wlr_output_add_software_cursors_to_render_pass(output, render_pass, &render_data.damage);
    fx_pass->output_buffer->effect_capture_owner = scene_output;
    if (!fx_render_pass_save_effect_capture(fx_pass)) {
      // Show the unfiltered frame rather than a filtered one without its capture.
      render_data.effect_capture = true;
    }
    // Start the display composition from the background again.
    wlr_render_pass_add_rect(render_pass, &(struct wlr_render_rect_options){
        .box = {0, 0, buffer->width, buffer->height},
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        .color = {scene_output->scene->background_color[0], scene_output->scene->background_color[1],
                  scene_output->scene->background_color[2], scene_output->scene->background_color[3]},
        .clip = &render_data.damage,
    });
  }
```
`persistent_visible` is true when any in-place slot draws on this output (a node under a persistent effect) — the unfiltered composition is only worth its cost then (screen/cursor join the condition in Stage 6). The whole-output damage is required for the second composition: when this branch runs, `scene_output_damage_whole(scene_output)` must have been called before the damage ring was read — compute `output_effects`, `exclude_from_capture`, `capture_pending`, and `unfiltered_pass` right after the render list is built (where `persistent_visible` is known) and call `scene_output_damage_whole(scene_output)` there when `unfiltered_pass` holds; the block above then reuses those locals. `wlr_scene_output_set_effect_capture_policy(output, false)` on an output that has the addon keeps it (the addon may carry screen/cursor state from Stage 6); only the `true` transition creates it.

`output_effects_release_capture(scene_output)`: walk `fx_get_renderer(output->renderer)->buffers` and for each `fx_framebuffer` with `effect_capture_owner == scene_output` drop its `effect_capture_buffer` (same as the fork's `output_postprocess_release_captures`). Call it also from `scene_output_effects_destroy`.

Scanout: `scene_entry_try_direct_scanout` is attempted only when `list_len == 1` and `!persistent_visible` (Stage 1), so an unfiltered capture never coincides with scanout.

- [ ] **Step 5: Run the tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs'`
Expected: `effects-capture-policy` passes both halves; everything else stays green.

- [ ] **Step 6: Commit**

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): unfiltered capture composition with role-keyed feedback history"
```

---

### Task 5.3: Compositor: window and overlay slots, capture scene, snapshots, `in_capture`

**Files:**
- Modify: `src/view/effects.h`, `src/view/effects.cpp`, `src/view/view.h`, `src/view/view.cpp` (`syncAnimationShaders`, `beginCloseAnimation` :2315, `captureTree` users), `src/output/output.cpp` (`handleFrame` :1105-1107, `applyOutputEffects`), `src/scene/effect_registry.h/.cpp` (`applyOutputEffects` scaffold)
- Test: covered by `760_effect_window` (Task 5.4)

**Interfaces:**
- Produces (`ViewEffects::ApplyInput`): `wlr_scene_node* captureSurface = nullptr;` (the capture scene's surface tree node).
- Produces (`Output`): `void applyOutputEffects();` — sets `wlr_scene_output_set_effect_capture_policy(m_sceneOutput, config().effects.inCapture)` (Stage 6 adds screen/cursor to it). Called from the constructor after `m_sceneOutput` exists, and from `EffectRegistry::applyOutputEffects()` which `prepare()` calls last.

- [ ] **Step 1: Extend `ViewEffects::apply`**

In `effects.cpp`, after the border block and before the ledger bookkeeping:

```cpp
    // Window slot: the default or window_effect preset, regardless of focus. Overlay: the border preset's window
    // preset, only while the border effect applies.
    const EffectPreset* windowPreset = m_window.empty() ? nullptr : registry.presetConfig(m_window);
    fx_effect_shader* windowShader = windowPreset != nullptr ? registry.preset(m_window, EffectKind::Window) : nullptr;
    const EffectPreset* overlayPreset =
        active && preset != nullptr && !preset->overlay.empty() ? registry.presetConfig(preset->overlay) : nullptr;
    fx_effect_shader* overlayShader =
        overlayPreset != nullptr ? registry.preset(preset->overlay, EffectKind::Window) : nullptr;
    const auto bindWindowSlots = [&](wlr_scene_node* node) {
      if (node == nullptr) {
        return;
      }
      if (windowShader != nullptr) {
        fx_animation_parameters parameters{};
        registry.fillTimeUniforms(parameters, input.seconds, *windowPreset, windowShader);
        wlr_scene_node_set_animation(node, FX_SLOT_WINDOW, windowShader, &parameters);
      } else {
        wlr_scene_node_set_animation(node, FX_SLOT_WINDOW, nullptr, nullptr);
      }
      if (overlayShader != nullptr) {
        fx_animation_parameters parameters{};
        const bool advancing = preset->animated && preset->speed > 0.0F;
        registry.fillTimeUniforms(parameters, advancing ? input.seconds * preset->speed : 0.0F, *overlayPreset, overlayShader);
        wlr_scene_node_set_animation(node, FX_SLOT_OVERLAY, overlayShader, &parameters);
      } else {
        wlr_scene_node_set_animation(node, FX_SLOT_OVERLAY, nullptr, nullptr);
      }
    };
    bindWindowSlots(input.surface);
    // Isolated toplevel captures get window slots only when effects are included in captures.
    if (input.captureSurface != nullptr) {
      if (config().effects.inCapture) {
        bindWindowSlots(input.captureSurface);
      } else {
        wlr_scene_node_set_animation(input.captureSurface, FX_SLOT_WINDOW, nullptr, nullptr);
        wlr_scene_node_set_animation(input.captureSurface, FX_SLOT_OVERLAY, nullptr, nullptr);
      }
    }
    // Two instances on the surface node: the window slot (always advancing with the animation clock; window presets
    // have no animated/speed) and the overlay slot (advancing only while the border's clock does). Keyed by the
    // slot address so an overlay that reads time keeps requesting frames on a border that does not.
    const void* windowOwner = input.surface;
    const void* overlayOwner = input.surface != nullptr ? static_cast<const void*>(&input.surface->addons) : nullptr;
    if (windowShader != nullptr && input.surface != nullptr) {
      track(windowOwner);
      registry.updateInstance(
          windowOwner,
          {
              .output = input.output,
              .visible = nodeVisibleOn(input.surface, input.outputBox),
              .readsTime = fx_effect_shader_reads(windowShader, "umbriel_time"),
              .advancing = input.clockAdvancing,
          }
      );
    } else {
      untrack(windowOwner);
    }
    if (overlayShader != nullptr && input.surface != nullptr) {
      track(overlayOwner);
      registry.updateInstance(
          overlayOwner,
          {
              .output = input.output,
              .visible = nodeVisibleOn(input.surface, input.outputBox),
              .readsTime = fx_effect_shader_reads(overlayShader, "umbriel_time"),
              .advancing = preset->animated && preset->speed > 0.0F && input.clockAdvancing,
          }
      );
    } else {
      untrack(overlayOwner);
    }
```
The overlay owner is the surface node's `addons` member address: a distinct, stable address per node that no other instance uses. `detachNodes(surface, border)` (Stage 4) untracks `surface`, `&surface->addons`, and `border`. The merge-gate early return at the top also clears both slots on `surface`/`captureSurface` when `registry.active() > 0`.

`View::syncAnimationShaders`: pass `.captureSurface = m_captureScene != nullptr ? toplevelSurfaceTreeNode(&m_captureScene->tree, m_toplevel->base->surface) : nullptr` (only for the view's own trees, not for cards).

`View::beginCloseAnimation` (after the existing `wlr_scene_node_copy_animations_for_snapshot(&snap->node, &m_contentTree->node);`):
```cpp
    // Window and overlay effects live on the surface tree; the snapshot's content tree takes them over with time frozen.
    if (wlr_scene_node* surface = toplevelSurfaceTreeNode(m_contentTree, m_toplevel->base->surface)) {
      wlr_scene_node_copy_animations_for_snapshot(&content->node, surface);
    }
```

`Output::applyOutputEffects()`:
```cpp
  void Output::applyOutputEffects() {
    // Nothing configured: touch nothing (no addon, no scene calls). The registry's counts are zero too.
    const Effects& effects = config().effects;
    if (effects.presets.empty() && !effects.inCapture && m_server->effects().active() == 0) {
      return;
    }
    wlr_scene_output_set_effect_capture_policy(m_sceneOutput, effects.inCapture);
  }
```
called at the end of the constructor and from `EffectRegistry::applyOutputEffects()` (`for (auto& output : m_server->outputs()) output->applyOutputEffects();`) which `prepare()` calls last. Stage 6 keeps the same early return in front of the screen/cursor attachment. `Output::handleFrame`: after `sceneOptions.capture_sdr = ...` add `sceneOptions.effect_capture_pending = captureLocks > 0 && m_server->effects().active();` — `captureLocks` counts screencopy/image-copy render locks minus export-dmabuf frames, the same signal `capture_sdr` uses.

- [ ] **Step 2: Build and run the effect checks**

Run: `nix develop . --command bash -c 'just build && just test && just check 750 751 780 18 19'`
Expected: green.

- [ ] **Step 3: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(view): window and overlay effects with capture policy"
```

---

### Task 5.4: Harness check `760_effect_window`

**Files:**
- Create: `tests/harness/checks/760_effect_window.sh`

- [ ] **Step 1: Write the check**

```bash
#!/usr/bin/env bash
# A window preset rewrites a translucent window's pixels in place: at rest it sees the desktop through the window,
# inside an opening capture it shades window content over the live desktop, backdrop sampling returns after the capture
# ends, it survives the close snapshot, appears on overview cards, and window_effect overrides the default.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-window.png"
# Swap red and blue of whatever is under the window.
cat > "$UMBRIEL_RUNTIME_DIR/swap.glsl" <<'GLSL'
vec4 window(vec2 uv) { return umbriel_sample(uv).bgra; }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/green.glsl" <<'GLSL'
vec4 window(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(0.0, c.a, 0.0, c.a); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/hold.glsl" <<'GLSL'
// An opening animation that keeps the window at rest so its capture encloses the window effect.
vec4 animation(vec2 uv) { return umbriel_sample(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
backdrop = "#0000FFFF"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[animation]
duration_ms = 2000
curve = "linear"
[animation.windows_in]
style = "none"
effect = "hold"
[animation.windows_out]
enabled = true
duration_ms = 2000
[effects]
window = "swap"
[effects.preset.swap]
kind = "window"
shader = "swap.glsl"
[effects.preset.green]
kind = "window"
shader = "green.glsl"
[effects.preset.hold]
kind = "animation"
shader = "hold.glsl"
[[window_rule]]
match.title = "^window-(rest|override)$"
default_floating = true
[[window_rule]]
match.title = "^window-override$"
window_effect = "green"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  # 50% red: over the blue backdrop the desktop composite is (0.5, 0, 0.5); swapped it stays (0.5, 0, 0.5) but the
  # window's own pixels are (0.5, 0, 0) -> swapped (0, 0, 0.5), so the two contracts differ in the red channel.
  FILL_COLOR=0x80800000 "$UMBRIEL_UNMAP_CLIENT" "$1" 300 200 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]]
  read -r x y w h id < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
}
centre() { grim "$IMAGE"; "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y + h / 2))"; }

"$UMBRIEL" clock-freeze
spawn window-rest
# Inside the opening capture the program shades only window content: (0.5,0,0) -> swapped (0,0,0.5), then composited
# over blue: red ~0, blue ~255.
"$UMBRIEL" clock-advance 500
read -r r g b < <(centre)
if (( r > 20 || b < 200 )); then
  echo "inside the opening capture the effect did not shade window content alone: $r $g $b"
  exit 1
fi
# After the capture ends the program reads the desktop through the window: (0.5,0,0.5) swapped stays purple.
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle > /dev/null
read -r r g b < <(centre)
if (( r < 100 || r > 160 || b < 100 || b > 160 )); then
  echo "at rest the effect did not sample the desktop backdrop through the window: $r $g $b"
  exit 1
fi
# Overview cards carry the effect: the card is a copy of the window at half size; sample its centre.
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
purple=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r > 0.35 && r < 0.65 && b > 0.35 && b < 0.65 && g < 0.1')
if (( purple < 500 )); then
  echo "the overview card did not show the window effect: $purple purple pixels"
  exit 1
fi
"$UMBRIEL" msg overview-close > /dev/null
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle > /dev/null
# Close: the snapshot keeps the effect while it fades.
"$UMBRIEL" msg "window-close:$id" > /dev/null
"$UMBRIEL" clock-advance 200
read -r r g b < <(centre)
if (( r < 60 )); then
  echo "the close snapshot dropped the window effect: $r $g $b"
  exit 1
fi
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle > /dev/null
# window_effect on a rule replaces the default.
spawn window-override
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle > /dev/null
read -r r g b < <(centre)
if (( g < 100 || r > 20 )); then
  echo "window_effect did not override the default preset: $r $g $b"
  exit 1
fi
echo "in-place window effect at rest, inside a capture, on cards, in the close snapshot, and per rule verified"
```
`unmap-client` fills ARGB8888 with `FILL_COLOR`; confirm it premultiplies (`0x80800000` = 50% alpha, red 0x80) by reading `tests/harness/clients/unmap_client.cpp`; if it expects straight alpha, use `0x80FF0000`.

- [ ] **Step 2: Run under stress and gate the stage**

Run: `nix develop . --command bash -c 'just check 760 && just check-stress 760 8 && just format && git diff --exit-code && just lint && just test && just gpu-test && just check'`
Expected: green.

- [ ] **Step 3: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): window effect check"
```
