// Effect program kinds, generic uniforms, and the composition slots, rendered
// on a headless output. Each case name is a meson test; the executable
// returns 77 without an FP16-capable render node.
#include "render_fixture.h"
#include "render/fx_renderer/effect.h"
#include "umbrielfx/render/effect.h"
#include "umbrielfx/render/pass.h"
#include <wlr/util/transform.h>

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

	// An oversized count against a declared array must be rejected wholesale
	// (logged once, ignored), not clamped down to the declared size. Both
	// elements are read so every driver reports the declared active size.
	struct fx_effect_shader *oversized = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"uniform vec4 pal[2];\nvec4 animation(vec2 uv) { return uv.x < 2.0 ? pal[0] : pal[1]; }", "oversized-count");
	ok &= check(oversized != NULL, "oversized-count program compiles");
	struct fx_animation_parameters exact = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *pal_exact = fx_parameters_add_uniform(&exact, "pal", FX_UNIFORM_VEC4, 2);
	ok &= check(pal_exact != NULL, "a count matching the declared array size fits");
	if (pal_exact != NULL) {
		pal_exact->floats[0] = 1.0f; pal_exact->floats[1] = 0.0f; pal_exact->floats[2] = 0.0f; pal_exact->floats[3] = 1.0f; // red
		pal_exact->floats[4] = 0.0f; pal_exact->floats[5] = 0.0f; pal_exact->floats[6] = 1.0f; pal_exact->floats[7] = 1.0f; // blue
	}
	ok &= render_animation(fixture, oversized, &exact, 0, pixel);
	ok &= check(pixel[2] > 250 && pixel[0] < 5, "pal[0] binds red when count matches the declared array size");
	struct fx_animation_parameters over = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *pal_over = fx_parameters_add_uniform(&over, "pal", FX_UNIFORM_VEC4, 4);
	ok &= check(pal_over != NULL, "a count larger than the declared array size still fits fx_uniform storage");
	if (pal_over != NULL) {
		pal_over->floats[0] = 0.0f; pal_over->floats[1] = 1.0f; pal_over->floats[2] = 0.0f; pal_over->floats[3] = 1.0f; // green
	}
	ok &= render_animation(fixture, oversized, &over, 0, pixel);
	ok &= check(pixel[2] > 250 && pixel[1] < 5, "an oversized count is rejected, leaving the previous binding intact");
	fx_effect_shader_unref(oversized);

	// A name too long for the cache is skipped, never cached as a truncated alias of a real uniform.
	struct fx_effect_shader *long_name = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"uniform vec4 abcdefghijklmnopqrstuvwxyz0123456789ABCD;\n"
		"uniform float abcdefghijklmnopqrstuvwxyz01234;\n"
		"vec4 animation(vec2 uv) {\n"
		"  return vec4(abcdefghijklmnopqrstuvwxyz01234, 0.0, 0.0, 1.0) + abcdefghijklmnopqrstuvwxyz0123456789ABCD;\n"
		"}",
		"long-name");
	ok &= check(long_name != NULL, "long-uniform-name program compiles");
	ok &= check(!fx_effect_shader_reads(long_name, "abcdefghijklmnopqrstuvwxyz0123456789ABCD"),
		"a 40-character uniform name is not cached under a truncated alias");
	ok &= check(fx_effect_shader_reads(long_name, "abcdefghijklmnopqrstuvwxyz01234"),
		"the real 31-character uniform sharing that prefix is still cached under its own name");
	unsigned name_matches = 0;
	for (unsigned i = 0; i < long_name->uniform_count; i++) {
		if (strcmp(long_name->uniforms[i].name, "abcdefghijklmnopqrstuvwxyz01234") == 0) {
			name_matches++;
			ok &= check(long_name->uniforms[i].type == GL_FLOAT,
				"the cached entry for the 31-character name keeps its declared float type");
		}
	}
	ok &= check(name_matches == 1,
		"the 31-character name is cached exactly once, not aliased by the truncated 40-character name");
	struct fx_animation_parameters named = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *short_uniform = fx_parameters_add_uniform(&named, "abcdefghijklmnopqrstuvwxyz01234", FX_UNIFORM_FLOAT, 1);
	ok &= check(short_uniform != NULL, "the 31-character name fits fx_uniform storage");
	if (short_uniform != NULL) {
		short_uniform->floats[0] = 1.0f;
	}
	ok &= render_animation(fixture, long_name, &named, 0, pixel);
	ok &= check(pixel[2] > 250 && pixel[3] > 250, "binding the real short uniform by name is not blocked by the skipped long alias");
	fx_effect_shader_unref(long_name);

	// Hand-built entries whose count exceeds their own storage are rejected, leaving the previous binding.
	struct fx_effect_shader *bounded = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"uniform int steps[8]; uniform vec4 tints[9];\n"
		"vec4 animation(vec2 uv) { return vec4(tints[0].rgb * float(steps[0]), 1.0); }", "bounded-count");
	ok &= check(bounded != NULL, "bounded-count program compiles");
	struct fx_animation_parameters fitting = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *fitting_steps = fx_parameters_add_uniform(&fitting, "steps", FX_UNIFORM_INT, 1);
	struct fx_uniform *fitting_tints = fx_parameters_add_uniform(&fitting, "tints", FX_UNIFORM_VEC4, 1);
	ok &= check(fitting_steps != NULL && fitting_tints != NULL, "fitting entries fit");
	if (fitting_steps != NULL && fitting_tints != NULL) {
		fitting_steps->ints[0] = 1;
		fitting_tints->floats[0] = 1.0f; fitting_tints->floats[3] = 1.0f; // red
	}
	ok &= render_animation(fixture, bounded, &fitting, 0, pixel);
	ok &= check(pixel[2] > 250 && pixel[1] < 5, "fitting int and vec4 array entries bind");
	struct fx_animation_parameters too_many_ints = fitting;
	too_many_ints.uniforms[0].count = 5;   // ints[] holds 4
	too_many_ints.uniforms[0].ints[0] = 0;
	ok &= render_animation(fixture, bounded, &too_many_ints, 0, pixel);
	ok &= check(pixel[2] > 250, "an INT entry with count > 4 is rejected");
	struct fx_animation_parameters too_many_floats = fitting;
	too_many_floats.uniforms[1].count = 9;   // 36 floats; floats[] holds 32
	too_many_floats.uniforms[1].floats[0] = 0.0f; too_many_floats.uniforms[1].floats[1] = 1.0f; // green
	ok &= render_animation(fixture, bounded, &too_many_floats, 0, pixel);
	ok &= check(pixel[2] > 250 && pixel[1] < 5, "a float entry past FX_UNIFORM_FLOATS_MAX is rejected");
	fx_effect_shader_unref(bounded);
	return ok;
}

// Clears `target`, captures a green rect over `box` (buffer coordinates), and
// composites `shader` over it with `expand`.
static bool render_expand(struct fixture *fixture, struct wlr_buffer *target, struct fx_effect_shader *shader,
		const struct wlr_box *box, const struct wlr_box *logical_box, enum wl_output_transform transform, int expand) {
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, target, NULL);
	if (!check(pass != NULL, "pass")) {
		return false;
	}
	struct fx_gles_render_pass *fx_pass = fx_get_render_pass(pass);
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options) {
		.box = { .width = TEST_WIDTH, .height = TEST_HEIGHT }, .color = { 0 }, .blend_mode = WLR_RENDER_BLEND_MODE_NONE });
	bool ok = check(fx_render_pass_init_offscreen_buffers(pass, fixture->output), "offscreen buffers");
	ok &= check(fx_render_pass_begin_animation(fx_pass), "capture begins");
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options) {
		.box = *box, .color = { .r = 0, .g = 1, .b = 0, .a = 1 }, .blend_mode = WLR_RENDER_BLEND_MODE_NONE });
	fx_render_pass_end_animation(fx_pass, shader, &parameters, box, logical_box, transform, NULL, expand);
	ok &= check(wlr_render_pass_submit(pass), "submit");
	return ok;
}

static bool painted(struct fixture *fixture, struct wlr_buffer *target, int x, int y) {
	uint8_t pixel[4];
	return fixture_read_pixel(fixture, target, x, y, pixel) && pixel[2] > 250 && pixel[3] > 250;
}

static bool blank(struct fixture *fixture, struct wlr_buffer *target, int x, int y) {
	uint8_t pixel[4];
	return fixture_read_pixel(fixture, target, x, y, pixel) && pixel[2] < 5 && pixel[3] < 5;
}

static bool test_expand(struct fixture *fixture) {
	// Solid red everywhere the program is drawn: with expand, red must reach past the node box.
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return vec4(1.0, 0.0, 0.0, 1.0) * umbriel_expand.x * 4.0 + umbriel_sample(uv) * 0.0; }", "expand");
	struct fx_effect_shader *scaled = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return vec4(umbriel_scale, 0.0, 0.0, 1.0) + umbriel_sample(uv) * 0.0; }", "expand-scale");
	struct wlr_buffer *target = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, TEST_WIDTH, TEST_HEIGHT);
	if (!check(shader != NULL && scaled != NULL && target != NULL, "expand programs compile and the target exists")) {
		fx_effect_shader_unref(shader);
		fx_effect_shader_unref(scaled);
		wlr_buffer_drop(target);
		return false;
	}
	// expand = 2 logical px on an unscaled target: the drawn box is 8x8 at (4,4), so umbriel_expand.x == 0.25.
	const struct wlr_box box = { .x = 6, .y = 6, .width = 4, .height = 4 };
	bool ok = render_expand(fixture, target, shader, &box, &box, WL_OUTPUT_TRANSFORM_NORMAL, 2);
	ok &= check(painted(fixture, target, 8, 8) && painted(fixture, target, 4, 4),
		"the program paints the node box and its expand margin");
	ok &= check(blank(fixture, target, 2, 2), "nothing is drawn past the expanded box");

	// An 8x4 node at (4,6) on a 90-degree output: its buffer box is 4x8, and the
	// 2 px margin holds on both buffer axes with umbriel_scale == 1.
	const struct wlr_box logical = { .x = 4, .y = 6, .width = 8, .height = 4 };
	struct wlr_box rotated;
	wlr_box_transform(&rotated, &logical, wlr_output_transform_invert(WL_OUTPUT_TRANSFORM_90), TEST_WIDTH, TEST_HEIGHT);
	ok &= render_expand(fixture, target, scaled, &rotated, &logical, WL_OUTPUT_TRANSFORM_90, 2);
	const int left = rotated.x - 2, right = rotated.x + rotated.width + 1;
	const int top = rotated.y - 2, bottom = rotated.y + rotated.height + 1;
	const int mid_x = rotated.x + rotated.width / 2, mid_y = rotated.y + rotated.height / 2;
	ok &= check(painted(fixture, target, mid_x, mid_y), "the rotated node box is painted with umbriel_scale 1");
	ok &= check(painted(fixture, target, left, mid_y) && painted(fixture, target, right, mid_y),
		"the rotated margin covers 2 px on the buffer x axis");
	ok &= check(painted(fixture, target, mid_x, top) && painted(fixture, target, mid_x, bottom),
		"the rotated margin covers 2 px on the buffer y axis");
	ok &= check(blank(fixture, target, left - 1, mid_y) && blank(fixture, target, right + 1, mid_y)
		&& blank(fixture, target, mid_x, top - 1) && blank(fixture, target, mid_x, bottom + 1),
		"nothing is drawn past the rotated expanded box");
	wlr_buffer_drop(target);
	fx_effect_shader_unref(shader);
	fx_effect_shader_unref(scaled);
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

// A persistent slot on one node must not disturb an unrelated node's culling
// and must draw only inside its own bounds. Layout (16x16 output):
//   background: opaque blue rect covering everything
//   effect node: opaque rect 4x4 at (2,2) with a persistent (window slot) program returning green
//   bystander: opaque red rect 4x4 at (10,10), no effect
static bool test_persistent_scene(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 }, red[4] = { 1, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
	struct wlr_scene_rect *background = wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct wlr_scene_rect *effect = wlr_scene_rect_create(&scene->tree, 4, 4, white);
	wlr_scene_node_set_position(&effect->node, 2, 2);
	struct wlr_scene_rect *bystander = wlr_scene_rect_create(&scene->tree, 4, 4, red);
	wlr_scene_node_set_position(&bystander->node, 10, 10);
	struct fx_effect_shader *green = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }", "persistent-scene");
	bool ok = check(green != NULL, "window program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&effect->node, FX_SLOT_WINDOW, green, &parameters);
	// Only the effect's own subtree stops culling: the bystander still hides
	// the background beneath it.
	ok &= check(pixman_region32_contains_point(&background->node.visible, 3, 3, NULL),
		"the background stays visible under the effect node");
	ok &= check(!pixman_region32_contains_point(&background->node.visible, 12, 12, NULL),
		"the bystander still culls the background");

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
	struct fx_offscreen_buffers *captured = fx_offscreen_buffers_try_get(fixture->output);
	ok &= check(captured != NULL && captured->animation_buffers[0] != NULL,
		"the window slot's capture holds an animation buffer");

	// With the slot removed nothing keeps the scene's effect list: the next
	// render must not re-add offscreen buffers (a second render succeeds and the
	// output's fx_offscreen_buffers hold no animation buffers).
	wlr_scene_node_set_animation(&effect->node, FX_SLOT_WINDOW, NULL, NULL);
	struct wlr_output_state again;
	struct wlr_buffer *plain = fixture_render_scene(fixture, scene_output, &again);
	ok &= check(plain != NULL, "scene renders after the slot is removed");
	struct fx_offscreen_buffers *fbos = fx_offscreen_buffers_try_get(fixture->output);
	ok &= check(fbos == NULL || fbos->animation_buffers[0] == NULL, "animation buffers are released without effects");
	if (plain != NULL) wlr_buffer_unlock(plain);
	wlr_output_state_finish(&again);

	// Scene teardown finishes the root's effect state before the nodes that
	// still carry slots.
	wlr_scene_node_set_animation(&effect->node, FX_SLOT_WINDOW, green, &parameters);
	fx_effect_shader_unref(green);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

// A persistent window effect reads its input under an opaque node above it.
// Layout: blue background, a white 8x8 effect node at (4,4) whose window slot
// mirrors it horizontally, and an opaque red 8x8 rect at (8,4) over its right half.
static bool test_occlusion(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 }, red[4] = { 1, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct wlr_scene_rect *effect = wlr_scene_rect_create(&scene->tree, 8, 8, white);
	wlr_scene_node_set_position(&effect->node, 4, 4);
	struct wlr_scene_rect *cover = wlr_scene_rect_create(&scene->tree, 8, 8, red);
	wlr_scene_node_set_position(&cover->node, 8, 4);
	struct fx_effect_shader *mirror = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return umbriel_sample(vec2(1.0 - uv.x, uv.y)); }", "occlusion");
	bool ok = check(mirror != NULL, "mirror program compiles");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_node_set_animation(&effect->node, FX_SLOT_WINDOW, mirror, &parameters);
	ok &= check(pixman_region32_contains_point(&effect->node.visible, 10, 8, NULL),
		"the covered half stays in the effect node's visible region");

	struct wlr_output_state state;
	struct wlr_buffer *rendered = fixture_render_scene(fixture, scene_output, &state);
	ok &= check(rendered != NULL, "scene renders");
	if (rendered != NULL) {
		uint8_t mirrored[4], covered[4];
		ok &= fixture_read_pixel(fixture, rendered, 5, 8, mirrored);
		ok &= fixture_read_pixel(fixture, rendered, 10, 8, covered);
		ok &= check(mirrored[0] > 250 && mirrored[1] > 250 && mirrored[2] > 250,
			"the covered input is captured and mirrored into the left half");
		ok &= check(covered[2] > 250 && covered[0] < 5 && covered[1] < 5, "the red rect still covers the right half");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	fx_effect_shader_unref(mirror);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static struct wlr_swapchain *create_swapchain(struct fixture *fixture) {
	const struct wlr_drm_format *format = get_render_format(fixture, DRM_FORMAT_ARGB8888);
	return format != NULL ? wlr_swapchain_create(fixture->allocator, TEST_WIDTH, TEST_HEIGHT, format) : NULL;
}

// Builds a frame on `swapchain`, which outlives the frame so buffer age limits
// render damage, and acknowledges its damage as a commit would. The caller
// unlocks the returned buffer and finishes `state`.
static struct wlr_buffer *render_frame(struct wlr_scene_output *scene_output, struct wlr_swapchain *swapchain,
		struct wlr_output_state *state) {
	wlr_output_state_init(state);
	struct wlr_scene_output_state_options options = { .swapchain = swapchain };
	if (!wlr_scene_output_build_state(scene_output, state, &options) || state->buffer == NULL) {
		return NULL;
	}
	wlr_scene_output_acknowledge_damage_for_test(scene_output, state);
	return wlr_buffer_lock(state->buffer);
}

// One whole-damage frame per swapchain buffer, so the next frame carries only its own damage.
static bool warm_up(struct wlr_scene_output *scene_output, struct wlr_swapchain *swapchain) {
	bool ok = true;
	for (int i = 0; i < 4; i++) {
		wlr_scene_output_damage_whole_for_test(scene_output);
		struct wlr_output_state state;
		struct wlr_buffer *buffer = render_frame(scene_output, swapchain, &state);
		ok &= buffer != NULL;
		if (buffer != NULL) {
			wlr_buffer_unlock(buffer);
		}
		wlr_output_state_finish(&state);
	}
	return check(ok, "warm-up frames render");
}

// Renders one frame and checks its commit damage extents against (x1,y1)-(x2,y2), x2/y2 exclusive.
static bool frame_damage_is(struct wlr_scene_output *scene_output, struct wlr_swapchain *swapchain,
		int x1, int y1, int x2, int y2, const char *message) {
	struct wlr_output_state state;
	struct wlr_buffer *buffer = render_frame(scene_output, swapchain, &state);
	const pixman_box32_t *extents = pixman_region32_extents(&state.damage);
	bool ok = buffer != NULL && (state.committed & WLR_OUTPUT_STATE_DAMAGE)
		&& extents->x1 == x1 && extents->y1 == y1 && extents->x2 == x2 && extents->y2 == y2;
	if (!ok) {
		fprintf(stderr, "  damage (%d,%d)-(%d,%d), expected (%d,%d)-(%d,%d)\n",
			extents->x1, extents->y1, extents->x2, extents->y2, x1, y1, x2, y2);
	}
	if (buffer != NULL) {
		wlr_buffer_unlock(buffer);
	}
	wlr_output_state_finish(&state);
	return check(ok, message);
}

static const char kGainSource[] = "uniform float gain;\nvec4 window(vec2 uv) { return umbriel_sample(uv) * gain; }";

// A persistent parameter change damages only its own node's box.
static bool test_damage_confinement(struct fixture *fixture) {
	struct wlr_swapchain *swapchain = create_swapchain(fixture);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW, kGainSource, "gain");
	if (!check(swapchain != NULL && shader != NULL, "swapchain and gain program")) {
		wlr_swapchain_destroy(swapchain);
		fx_effect_shader_unref(shader);
		return false;
	}
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float white[4] = { 1, 1, 1, 1 };
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 6, 6, white);
	wlr_scene_node_set_position(&rect->node, 5, 5);
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *gain = fx_parameters_add_uniform(&parameters, "gain", FX_UNIFORM_FLOAT, 1);
	gain->floats[0] = 1.0f;
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, shader, &parameters);
	bool ok = warm_up(scene_output, swapchain);
	gain->floats[0] = 0.5f;
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, shader, &parameters);
	ok &= frame_damage_is(scene_output, swapchain, 5, 5, 11, 11, "a uniform change damages only the effect box");
	fx_effect_shader_unref(shader);
	wlr_scene_node_destroy(&scene->tree.node);
	wlr_swapchain_destroy(swapchain);
	return ok;
}

// Damage touching a persistent effect box grows to the whole box, and on to
// every box the grown damage touches.
static bool test_whole_box_invalidation(struct fixture *fixture) {
	struct wlr_swapchain *swapchain = create_swapchain(fixture);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW, kGainSource, "gain");
	if (!check(swapchain != NULL && shader != NULL, "swapchain and gain program")) {
		wlr_swapchain_destroy(swapchain);
		fx_effect_shader_unref(shader);
		return false;
	}
	const float white[4] = { 1, 1, 1, 1 }, red[4] = { 1, 0, 0, 1 };
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	struct fx_uniform *gain = fx_parameters_add_uniform(&parameters, "gain", FX_UNIFORM_FLOAT, 1);
	gain->floats[0] = 1.0f;

	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 6, 6, white);
	wlr_scene_node_set_position(&rect->node, 5, 5);
	struct wlr_scene_rect *marker = wlr_scene_rect_create(&scene->tree, 1, 1, red);
	wlr_scene_node_set_position(&marker->node, 6, 6);
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, shader, &parameters);
	bool ok = warm_up(scene_output, swapchain);
	wlr_scene_node_set_position(&marker->node, 7, 7);
	ok &= frame_damage_is(scene_output, swapchain, 5, 5, 11, 11, "damage inside an effect box covers the whole box");
	wlr_scene_node_destroy(&scene->tree.node);

	// A at (2,2) and B at (6,6), both 6x6, overlap at (6,6)-(8,8); the marker at (3,3) is in A only.
	scene = wlr_scene_create();
	scene_output = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_rect *a = wlr_scene_rect_create(&scene->tree, 6, 6, white);
	wlr_scene_node_set_position(&a->node, 2, 2);
	struct wlr_scene_rect *b = wlr_scene_rect_create(&scene->tree, 6, 6, white);
	wlr_scene_node_set_position(&b->node, 6, 6);
	marker = wlr_scene_rect_create(&scene->tree, 1, 1, red);
	wlr_scene_node_set_position(&marker->node, 3, 3);
	wlr_scene_node_set_animation(&a->node, FX_SLOT_WINDOW, shader, &parameters);
	wlr_scene_node_set_animation(&b->node, FX_SLOT_WINDOW, shader, &parameters);
	ok &= warm_up(scene_output, swapchain);
	const float green[4] = { 0, 1, 0, 1 };
	wlr_scene_rect_set_color(marker, green);
	ok &= frame_damage_is(scene_output, swapchain, 2, 2, 12, 12, "damage in A grows through A to the overlapping B");
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(shader);
	wlr_swapchain_destroy(swapchain);
	return ok;
}

// A transient slot keeps whole-output damage every frame and never culls what its node covers.
static bool test_transient_policy(struct fixture *fixture) {
	struct wlr_swapchain *swapchain = create_swapchain(fixture);
	struct fx_effect_shader *identity = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return umbriel_sample(uv); }", "transient-identity");
	struct fx_effect_shader *clear = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return umbriel_sample(uv) * 0.0; }", "transient-clear");
	bool ok = check(swapchain != NULL && identity != NULL && clear != NULL, "swapchain and transient programs");
	if (!ok) {
		wlr_swapchain_destroy(swapchain);
		fx_effect_shader_unref(identity);
		fx_effect_shader_unref(clear);
		return false;
	}
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 }, red[4] = { 1, 0, 0, 1 };
	struct wlr_scene_rect *background = wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 8, 8, red);
	wlr_scene_node_set_position(&rect->node, 4, 4);
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1, .transition_id = 1 };
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOWS_IN, identity, &parameters);
	ok &= warm_up(scene_output, swapchain);
	ok &= frame_damage_is(scene_output, swapchain, 0, 0, TEST_WIDTH, TEST_HEIGHT,
		"an unchanged frame with a transient slot is damaged whole");
	ok &= check(pixman_region32_contains_point(&background->node.visible, 8, 8, NULL),
		"the background under the transient node is not culled");

	// A program that drops its input shows whatever was drawn beneath the node.
	parameters.transition_id = 2;
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOWS_IN, clear, &parameters);
	struct wlr_output_state state;
	struct wlr_buffer *rendered = render_frame(scene_output, swapchain, &state);
	ok &= check(rendered != NULL, "transient frame renders");
	if (rendered != NULL) {
		uint8_t under[4];
		ok &= fixture_read_pixel(fixture, rendered, 8, 8, under);
		ok &= check(under[0] > 250 && under[2] < 5, "the covered background was drawn into the target");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	fx_effect_shader_unref(identity);
	fx_effect_shader_unref(clear);
	wlr_scene_node_destroy(&scene->tree.node);
	wlr_swapchain_destroy(swapchain);
	return ok;
}

enum margin_removal { MARGIN_DISABLE, MARGIN_DISABLE_PARENT, MARGIN_DESTROY };

// Disabling or destroying a node damages its drawn box including the expand margin.
static bool test_margin_damage(struct fixture *fixture) {
	struct wlr_swapchain *swapchain = create_swapchain(fixture);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return umbriel_sample(uv); }", "margin");
	bool ok = check(swapchain != NULL && shader != NULL, "swapchain and drag program");
	if (!ok) {
		wlr_swapchain_destroy(swapchain);
		fx_effect_shader_unref(shader);
		return false;
	}
	static const char *const kNames[] = {
		[MARGIN_DISABLE] = "disabling the node damages its expand margin",
		[MARGIN_DISABLE_PARENT] = "disabling an ancestor damages a descendant's expand margin",
		[MARGIN_DESTROY] = "destroying the node damages its expand margin",
	};
	for (int removal = MARGIN_DISABLE; removal <= MARGIN_DESTROY; removal++) {
		struct wlr_scene *scene = wlr_scene_create();
		struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
		struct wlr_scene_tree *parent = wlr_scene_tree_create(&scene->tree);
		const float white[4] = { 1, 1, 1, 1 };
		struct wlr_scene_rect *rect = wlr_scene_rect_create(parent, 6, 6, white);
		wlr_scene_node_set_position(&rect->node, 5, 5);
		struct fx_animation_parameters parameters = {
			.progress = 1, .linear_progress = 1, .direction = 1, .transition_id = 1, .expand = 3 };
		wlr_scene_node_set_animation(&rect->node, FX_SLOT_DRAG, shader, &parameters);
		ok &= warm_up(scene_output, swapchain);
		// A disabled node's slot is cleared too, so the frame carries no transient whole-output damage.
		switch (removal) {
		case MARGIN_DISABLE:
			wlr_scene_node_set_enabled(&rect->node, false);
			wlr_scene_node_set_animation(&rect->node, FX_SLOT_DRAG, NULL, NULL);
			break;
		case MARGIN_DISABLE_PARENT:
			wlr_scene_node_set_enabled(&parent->node, false);
			wlr_scene_node_set_animation(&rect->node, FX_SLOT_DRAG, NULL, NULL);
			break;
		case MARGIN_DESTROY:
			wlr_scene_node_destroy(&rect->node);
			break;
		}
		ok &= frame_damage_is(scene_output, swapchain, 2, 2, 14, 14, kNames[removal]);
		wlr_scene_node_destroy(&scene->tree.node);
	}
	fx_effect_shader_unref(shader);
	wlr_swapchain_destroy(swapchain);
	return ok;
}

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
	} else if (strcmp(argv[1], "expand") == 0) {
		ok = test_expand(&fixture);
	} else if (strcmp(argv[1], "renderer-destroy") == 0) {
		ok = test_renderer_destroy(&fixture);
	} else if (strcmp(argv[1], "persistent-scene") == 0) {
		ok = test_persistent_scene(&fixture);
	} else if (strcmp(argv[1], "occlusion") == 0) {
		ok = test_occlusion(&fixture);
	} else if (strcmp(argv[1], "damage-confinement") == 0) {
		ok = test_damage_confinement(&fixture);
	} else if (strcmp(argv[1], "whole-box-invalidation") == 0) {
		ok = test_whole_box_invalidation(&fixture);
	} else if (strcmp(argv[1], "transient-policy") == 0) {
		ok = test_transient_policy(&fixture);
	} else if (strcmp(argv[1], "margin-damage") == 0) {
		ok = test_margin_damage(&fixture);
	} else if (strcmp(argv[1], "border-geometry") == 0) {
		ok = test_border_geometry(&fixture);
	} else {
		fprintf(stderr, "unknown case: %s\n", argv[1]);
		ok = false;
	}
	fixture_finish(&fixture);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
