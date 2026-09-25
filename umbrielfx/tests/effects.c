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
