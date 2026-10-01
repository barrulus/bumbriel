// Authored grid geometry feeds a preallocated native shadow kernel.
#include "render_fixture.h"
#include <wlr/util/transform.h>
#include "render/fx_renderer/scene_program.h"
#include "render/fx_renderer/effect.h"

#define WIDTH 64
#define HEIGHT 48
#define PAD 12

static bool read_companion(struct fixture *fixture, struct wlr_buffer *buffer, uint8_t *pixels) {
	struct fx_framebuffer *fb = fx_framebuffer_get_or_create(fx_get_renderer(fixture->renderer), buffer);
	if (fb->drm_format != DRM_FORMAT_ABGR16161616F)
		return read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, WIDTH * 4, pixels);
	uint16_t half[WIDTH * HEIGHT * 4];
	if (!read_buffer(fixture, buffer, DRM_FORMAT_ABGR16161616F, WIDTH * 8, half)) return false;
	for (unsigned i = 0; i < WIDTH * HEIGHT * 4; i++) {
		int exponent = (half[i] >> 10) & 31;
		float value = exponent == 0 ? ldexpf(half[i] & 1023, -24)
			: ldexpf(1.0f + (half[i] & 1023) / 1024.0f, exponent - 15);
		pixels[i] = lroundf(fminf(1, fmaxf(0, half[i] & 0x8000 ? -value : value)) * 255);
	}
	return true;
}

static bool compare(struct fixture *fixture, struct wlr_buffer *a, struct wlr_buffer *b, const char *label) {
	uint8_t pa[WIDTH * HEIGHT * 4], pb[sizeof(pa)];
	if (!read_companion(fixture, a, pa) ||
			!read_companion(fixture, b, pb)) return false;
	for (size_t i = 0; i < sizeof(pa); i++) {
		if (abs(pa[i] - pb[i]) > 1) {
			fprintf(stderr, "%s byte %zu authored=%u oracle=%u\n", label, i, pa[i], pb[i]);
			return false;
		}
	}
	return true;
}

static bool oracle(struct fixture *fixture, struct wlr_buffer *buffer, float progress,
		const struct fx_scene_shadow *shadow, int left, enum wl_output_transform transform) {
	const int width = WIDTH + PAD * 2, height = HEIGHT + PAD * 2;
	uint8_t *mask = calloc((size_t)width * height, 4);
	if (!mask) return false;
	// Independently rasterize the piecewise-linear top/bottom grid boundaries
	// at pixel centers. The shader's formation mask clips local vertical UV.
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			struct wlr_box point = {x - PAD, y - PAD, 1, 1};
			wlr_box_transform(&point, &point, transform, WIDTH, HEIGHT);
			float u = (point.x + 0.5f - left) / 32;
			float bend = (1 - progress) * 8 * (1 - fabsf(u * 2 - 1));
			float v = (point.y + 0.5f - 12 - bend) / 16;
			if (u >= 0 && u <= 1 && v >= 1 - progress && v <= 1) {
				memset(mask + ((size_t)y * width + x) * 4, 255, 4);
			}
		}
	}
	struct wlr_texture *texture = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888,
		width * 4, width, height, mask);
	free(mask);
	struct fx_offscreen_buffers scratch = {.renderer = fx_get_renderer(fixture->renderer), .allocator = fixture->allocator};
	struct wlr_render_pass *wlr_pass = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
	if (!texture || !wlr_pass) return false;
	struct fx_gles_render_pass *pass = fx_get_render_pass(wlr_pass);
	pass->fx_offscreen_buffers = &scratch;
	pass->working_space = pass->output_buffer->drm_format == DRM_FORMAT_ABGR16161616F;
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	struct wlr_box box = {-PAD, -PAD, width, height};
	bool ok = fx_render_pass_begin_capture(pass, &box) && fx_render_pass_begin_animation(pass);
	if (ok) {
		wlr_render_pass_add_texture(wlr_pass, &(struct wlr_render_texture_options){
			.texture = texture, .dst_box = {0, 0, width, height},
			.blend_mode = WLR_RENDER_BLEND_MODE_NONE, .filter_mode = WLR_SCALE_FILTER_NEAREST,
		});
		ok = fx_render_pass_end_animation_shadow(pass, shadow->softness,
			shadow->offset[0], shadow->offset[1], shadow->color, NULL);
		fx_render_pass_end_capture(pass, &box, NULL);
	}
	ok = wlr_render_pass_submit(wlr_pass) && ok;
	wlr_texture_destroy(texture);
	fx_offscreen_buffers_finish_local(&scratch);
	return ok;
}

static bool light_oracle(struct fixture *fixture, struct wlr_buffer *buffer, float progress,
		const struct fx_effect_light *light) {
	const int width = WIDTH + 2 * PAD, height = HEIGHT + 2 * PAD;
	uint8_t *pixels = calloc((size_t)width * height, 4);
	if (!pixels) return false;
	for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
		float u = (x + 0.5f - PAD - 16) / 32;
		float bend = (1 - progress) * 8 * (1 - fabsf(u * 2 - 1));
		float v = (y + 0.5f - PAD - 12 - bend) / 16;
		if (u >= 0 && u <= 1 && v >= 1 - progress && v <= 1 &&
				(u < 0.125f || u > 0.875f || v < 0.25f || v > 0.75f)) {
			memset(pixels + ((size_t)y * width + x) * 4, 255, 4);
		}
	}
	struct wlr_buffer *source = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, width, height);
	struct wlr_texture *cpu = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888, width * 4, width, height, pixels);
	free(pixels);
	struct wlr_render_pass *copy = source ? wlr_renderer_begin_buffer_pass(fixture->renderer, source, NULL) : NULL;
	if (!cpu || !copy) return false;
	wlr_render_pass_add_texture(copy, &(struct wlr_render_texture_options){.texture = cpu,
		.dst_box = {0, 0, width, height}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
	bool ok = wlr_render_pass_submit(copy);
	wlr_texture_destroy(cpu);
	struct wlr_texture *texture = wlr_texture_from_buffer(fixture->renderer, source);
	struct fx_effect_light_cache *cache = fx_effect_light_cache_create(fx_get_renderer(fixture->renderer));
	ok &= texture && cache && fx_effect_light_cache_prepare_scene(cache, width, height, light->spread);
	struct wlr_render_pass *wlr_pass = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
	if (ok && wlr_pass) {
		struct fx_gles_render_pass *pass = fx_get_render_pass(wlr_pass);
		pass->working_space = pass->output_buffer->drm_format == DRM_FORMAT_ABGR16161616F;
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		ok &= fx_render_pass_emit_scene_light(pass, cache, texture, light, 1);
		fx_render_pass_add_effect_light(pass, cache, light, &(struct wlr_box){-PAD, -PAD, width, height}, NULL);
	}
	ok &= wlr_pass && wlr_render_pass_submit(wlr_pass);
	fx_effect_light_cache_destroy(cache);
	wlr_texture_destroy(texture);
	wlr_buffer_drop(source);
	return ok;
}

static unsigned difference(struct fixture *fixture, struct wlr_buffer *a, struct wlr_buffer *b) {
	uint8_t pa[WIDTH * HEIGHT * 4], pb[sizeof(pa)];
	if (!read_companion(fixture, a, pa) ||
			!read_companion(fixture, b, pb)) return UINT32_MAX;
	unsigned total = 0;
	for (size_t i = 0; i < sizeof(pa); i++) total += abs(pa[i] - pb[i]);
	return total;
}

static bool native_shadow(struct fixture *fixture, struct wlr_buffer *buffer,
		const struct fx_scene_shadow *shadow) {
	struct wlr_render_pass *wlr_pass = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
	if (!wlr_pass) return false;
	fx_get_render_pass(wlr_pass)->working_space = fx_get_render_pass(wlr_pass)->output_buffer->drm_format == DRM_FORMAT_ABGR16161616F;
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	fx_render_pass_add_box_shadow(fx_get_render_pass(wlr_pass), &(struct fx_render_box_shadow_options){
		.box = {16 - 5 + 2, 12 - 5 + 1, 32 + 10, 16 + 10},
		.clipped_region.area = {16, 12, 32, 16},
		.blur_sigma = 5,
		.color = {shadow->color[0], shadow->color[1], shadow->color[2], shadow->color[3]},
	});
	return wlr_render_pass_submit(wlr_pass);
}

static bool experiment(struct fixture *fixture, bool working) {
	fprintf(stderr, "companion working_space=%d\n", working);
	struct wlr_buffer *buffer = fx_scene_buffer_create(fixture->renderer, fixture->allocator, WIDTH, HEIGHT, working);
	struct wlr_buffer *reference = fx_scene_buffer_create(fixture->renderer, fixture->allocator, WIDTH, HEIGHT, working);
	struct fx_scene_target *target = fx_scene_target_create(fixture->renderer, buffer, false);
	const struct fx_scene_sources sources = {
		.vertex = "vec4 transition_vertex(vec2 uv){vec2 xy=umbriel_current_box.xy+uv*umbriel_current_box.zw;"
			"xy.y+=(1.0-umbriel_progress)*8.0*(1.0-abs(uv.x*2.0-1.0));"
			"return vec4(xy/umbriel_output_size*2.0-1.0,0.0,1.0);}",
		.fragment = "vec4 transition_fragment(vec2 uv,vec2 out_uv){"
			"if(umbriel_item_kind==4 && uv.x>=0.125 && uv.x<=0.875 && uv.y>=0.25 && uv.y<=0.75)return vec4(0.0);"
			"return uv.y<1.0-umbriel_progress ? vec4(0.0):umbriel_sample_item(uv);}",
	};
	struct fx_scene_program *program = fx_scene_program_create(fixture->renderer, FX_SCENE_WINDOWS, &sources, NULL, 0);
	struct fx_scene_mesh mesh = {0};
	uint8_t white[] = {255, 255, 255, 255};
	struct wlr_texture *geometry = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888, 4, 1, 1, white);
	struct fx_scene_shadow shadow = {.softness = 5, .offset = {2, 1}, .color = {0.2f, 0.4f, 0.8f, 0.6f}};
	struct fx_scene_draw draw = {
		.item = {.kind = FX_SCENE_SHADOW, .token = 1, .current_box = {16, 12, 32, 16}},
		.input.texture = geometry, .mesh = &mesh, .shadow = &shadow,
	};
	struct fx_scene_frame frame = {.output_size = {WIDTH, HEIGHT}, .scale = 1, .progress = 1};
	bool ok = target && reference && program && geometry && fx_scene_mesh_create(&mesh, 8, 8, 1);
	ok &= check(!fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1), "unprepared companion rejects before drawing");
	ok &= check(fx_scene_target_shadow_bytes(target, PAD) == (WIDTH + 2 * PAD) * (HEIGHT + 2 * PAD) * (working ? 24 : 12),
		"shadow scratch reservation exactly accounts three padded RGBA targets");
	ok &= check(fx_scene_target_prepare_shadow(target, PAD), "prepare shadow storage and native shaders before rendering");
	ok &= check(!fx_scene_target_prepare_shadow(target, PAD + 1), "prepared topology cannot resize during lease");
	struct fx_scene_target *original_target = target;
	struct wlr_buffer *original_buffer = buffer;
	buffer = create_output_buffer(fixture, working ? DRM_FORMAT_ABGR16161616F : DRM_FORMAT_ARGB8888, WIDTH, HEIGHT);
	target = fx_scene_target_create(fixture->renderer, buffer, false);
	ok &= check(target && fx_scene_target_share_scratch(target, original_target), "role/version target shares prepared companion scratch");

	for (unsigned shifted = 0; ok && shifted < 2; shifted++) {
		draw.item.current_box[0] = shifted ? -10 : 16;
		for (unsigned step = 0; ok && step < 3; step++) {
			frame.progress = 1 - step * 0.5f;
			ok &= check(fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1), "deformed geometry shadow renders");
			ok &= oracle(fixture, reference, frame.progress, &shadow, draw.item.current_box[0], WL_OUTPUT_TRANSFORM_NORMAL);
			ok &= compare(fixture, buffer, reference, "independent rasterized geometry shadow");
		}
	}
	frame.progress = 0.5f;
	draw.item.current_box[0] = 8;
	for (unsigned transform = 0; ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
		frame.output_transform = transform;
		frame.output_size[0] = (transform & 1) ? HEIGHT : WIDTH;
		frame.output_size[1] = (transform & 1) ? WIDTH : HEIGHT;
		ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1);
		ok &= oracle(fixture, reference, frame.progress, &shadow, 8, transform);
		ok &= compare(fixture, buffer, reference, "transformed independent grid shadow");
	}
	frame.output_transform = WL_OUTPUT_TRANSFORM_NORMAL;
	frame.output_size[0] = WIDTH;
	frame.output_size[1] = HEIGHT;
	// The ordinary analytic box shadow is retained independently of client
	// opacity. Native convergence is a policy weight, not a shader-name branch.
	draw.item.current_box[0] = 16;
	frame.progress = 1;
	ok &= native_shadow(fixture, reference, &shadow);
	ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1);
	unsigned baseline_error = difference(fixture, buffer, reference);
	fprintf(stderr, "mask-kernel/ordinary-analytic endpoint L1 difference: %u\n", baseline_error);
	shadow.native_texture = wlr_texture_from_buffer(fixture->renderer, reference);
	shadow.native_box[2] = WIDTH;
	shadow.native_box[3] = HEIGHT;
	unsigned previous = UINT32_MAX;
	const float weights[] = {0, 0.5f, 0.9f, 0.99f, 1};
	for (unsigned i = 0; ok && i < sizeof(weights) / sizeof(weights[0]); i++) {
		shadow.native_mix = weights[i];
		frame.progress = 0.95f + weights[i] * 0.05f;
		ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1);
		unsigned error = difference(fixture, buffer, reference);
		fprintf(stderr, "native shadow mix %.3f endpoint L1 difference: %u\n", weights[i], error);
		ok &= check(error <= previous, "retained analytic shadow converges monotonically as residual vanishes");
		previous = error;
	}
	ok &= compare(fixture, buffer, reference, "exact native analytic endpoint");
	wlr_texture_destroy(shadow.native_texture);
	shadow.native_texture = NULL;
	shadow.native_mix = 0;
	shadow.softness = PAD + 1;
	ok &= check(!fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1), "unreserved shadow halo rejects");
	shadow.softness = 5;
	struct fx_effect_light recipes[] = {
		{.enabled = true, .spread = 8, .intensity = 2, .threshold = 0},
		{.enabled = true, .spread = 8, .intensity = 2, .threshold = 1},
	};
	draw.shadow = NULL;
	draw.item.kind = FX_SCENE_EMISSION;
	draw.light = &recipes[0];
	ok &= check(!fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1), "light recipe must be preflighted");
	ok &= check(fx_scene_target_light_bytes(target, PAD, recipes, 2, 1) > fx_scene_target_shadow_bytes(target, PAD), "light reservation includes every fixed pyramid");
	ok &= check(fx_scene_target_prepare_light(target, PAD, recipes, 2, 1), "preallocate native light kernels and recipe pyramids");
	fx_scene_target_destroy(original_target);
	wlr_buffer_drop(original_buffer);
	ok &= check(fx_scene_target_prepare_shadow(target, PAD), "shared scratch survives original target destruction");

	for (unsigned recipe = 0; ok && recipe < 2; recipe++) {
		draw.light = &recipes[recipe];
		for (unsigned step = 0; ok && step < 3; step++) {
			frame.progress = 1 - step * 0.5f;
			ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1);
			ok &= light_oracle(fixture, reference, frame.progress, draw.light);
			ok &= compare(fixture, buffer, reference, "independently rasterized native deformed light");
		}
	}
	frame.scale = 2;
	ok &= check(!fx_scene_program_render(program, target, NULL, &frame, NULL, &draw, 1), "unprepared light scale rejects without resizing");
	frame.scale = 1;
	const float mask_box[] = {8, 8, 24, 16}, mask_corners[] = {1, 3, 5, 7};
	for (unsigned transform = 0; ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
		frame.output_transform = transform;
		frame.output_size[0] = transform & 1 ? HEIGHT : WIDTH;
		frame.output_size[1] = transform & 1 ? WIDTH : HEIGHT;
		ok &= check(fx_scene_target_mask(target, &frame, mask_box, mask_corners), "opaque geometry source with native asymmetric rounded corners");
		struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, reference, NULL);
		struct fx_gles_render_pass *fx_pass = fx_get_render_pass(pass);
		fx_pass->working_space = working;
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		struct fx_corner_radii radii = {1, 3, 5, 7};
		fx_corner_radii_transform(transform, &radii);
		struct wlr_box native_box = {8, 8, 24, 16};
		wlr_box_transform(&native_box, &native_box, wlr_output_transform_invert(transform),
			frame.output_size[0], frame.output_size[1]);
		fx_render_pass_add_rounded_rect(fx_pass, &(struct fx_render_rounded_rect_options){
			.base = {.box = native_box, .color = {1, 1, 1, 1}},
			.corners = fx_corner_radii_scale(radii, 1),
		});
		ok &= wlr_render_pass_submit(pass) && compare(fixture, buffer, reference, "opaque native geometry mask orientation");
	}
	fx_scene_program_unref(program);
	fx_scene_target_destroy(target);
	fx_scene_mesh_finish(&mesh);
	wlr_texture_destroy(geometry);
	wlr_buffer_drop(buffer);
	wlr_buffer_drop(reference);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) { fixture_finish(&fixture); return 77; }
	bool ok = experiment(&fixture, false) && experiment(&fixture, true);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
