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

	// An oversized count against a declared array must be rejected wholesale
	// (logged once, ignored), not clamped down to the declared size.
	struct fx_effect_shader *oversized = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"uniform vec4 pal[2];\nvec4 animation(vec2 uv) { return pal[0]; }", "oversized-count");
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

	// A uniform name at or beyond the cache's name limit must be skipped
	// entirely during caching, not truncated into a shorter, wrong name. The
	// 40-character name here shares its first 31 (FX_UNIFORM_NAME_MAX - 1)
	// characters with a real, differently-typed uniform declared first: a
	// truncate-and-cache bug would alias the two under one name and record
	// the long uniform's vec4 type against the short uniform's float
	// location, making a correct float bind to the short name look like a
	// type mismatch.
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
	// GL's active-uniform enumeration order is implementation-defined, so a
	// truncate-and-cache bug could file the 40-character uniform's entry
	// either before or after the real 31-character one; either way the name
	// must appear in the cache exactly once, with the real uniform's type.
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
	return ok;
}

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
	} else if (strcmp(argv[1], "expand") == 0) {
		ok = test_expand(&fixture);
	} else if (strcmp(argv[1], "renderer-destroy") == 0) {
		ok = test_renderer_destroy(&fixture);
	} else {
		fprintf(stderr, "unknown case: %s\n", argv[1]);
		ok = false;
	}
	fixture_finish(&fixture);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
