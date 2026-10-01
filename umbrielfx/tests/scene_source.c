// C0/G1 source-local composition experiment, not a released capture interface.
#include "render_fixture.h"
#include "types/wlr_scene.h"
#include "types/scene_source.h"
#include "render/fx_renderer/scene_program.h"
#include "umbrielfx/render/effect.h"

struct sample_counter {
	struct wl_listener listener;
	unsigned count;
};

static void sampled(struct wl_listener *listener, void *data) {
	struct sample_counter *counter = wl_container_of(listener, counter, listener);
	(void)data;
	counter->count++;
}

static bool compare(struct fixture *fixture, struct wlr_buffer *a, struct wlr_buffer *b,
		const char *message) {
	uint8_t left[TEST_WIDTH * TEST_HEIGHT * 4], right[sizeof(left)];
	if (!read_buffer(fixture, a, DRM_FORMAT_ARGB8888, TEST_WIDTH * 4, left) ||
			!read_buffer(fixture, b, DRM_FORMAT_ARGB8888, TEST_WIDTH * 4, right)) {
		return check(false, "source comparison readback");
	}
	for (size_t i = 0; i < sizeof(left); i++) {
		if (abs(left[i] - right[i]) > 1) {
			fprintf(stderr, "%s: byte %zu native=%u capture=%u\n", message, i, left[i], right[i]);
			return false;
		}
	}
	return true;
}

static bool test_source(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	wlr_scene_output_set_position(output, 40, -20);
	wlr_scene_node_set_position(&scene->tree.node, 40, -20);
	const float back[] = {0.15f, 0.25f, 0.45f, 1}, red[] = {0.5f, 0, 0, 0.5f};
	const float green[] = {0, 0.5f, 0, 0.5f}, white[] = {1, 1, 1, 1};
	const float dark[] = {0, 0, 0, 0.6f}, pink[] = {1, 0, 1, 1};
	struct wlr_scene_rect *backdrop = wlr_scene_rect_create(&scene->tree, 16, 16, back);
	struct wlr_scene_optimized_blur *optimized = wlr_scene_optimized_blur_create(&scene->tree, 16, 16);
	struct wlr_scene_tree *windows = wlr_scene_tree_create(&scene->tree);
	struct wlr_buffer *content = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_render_pass *content_pass = content ? wlr_renderer_begin_buffer_pass(fixture->renderer, content, NULL) : NULL;
	if (!content_pass) {
		wlr_scene_node_destroy(&scene->tree.node);
		if (content) {
			wlr_buffer_drop(content);
		}
		return false;
	}
	wlr_render_pass_add_rect(content_pass, &(struct wlr_render_rect_options){
		.box = {.width = 16, .height = 16}, .color = {.r = 0.2f, .g = 0.3f, .b = 0.7f, .a = 1},
	});
	bool content_ok = wlr_render_pass_submit(content_pass);
	struct wlr_scene_tree *clipped = wlr_scene_tree_create(windows);
	struct wlr_box clip = {0, 10, 4, 3};
	wlr_scene_tree_set_clip(clipped, &clip);
	struct wlr_scene_buffer *client = wlr_scene_buffer_create(clipped, content);
	wlr_buffer_drop(content);
	struct sample_counter counter = {.listener.notify = sampled};
	wl_signal_add(&client->events.output_sample, &counter.listener);
	struct wlr_scene_shadow *shadow = wlr_scene_shadow_create(windows, 12, 12, 2, 1, dark);
	wlr_scene_node_set_position(&shadow->node, 1, 2);
	struct wlr_scene_rect *first = wlr_scene_rect_create(windows, 9, 8, red);
	wlr_scene_node_set_position(&first->node, 2, 3);
	struct wlr_scene_blur *blur = wlr_scene_blur_create(windows, 7, 7);
	wlr_scene_node_set_position(&blur->node, 6, 6);
	struct wlr_scene_rect *second = wlr_scene_rect_create(windows, 7, 7, green);
	wlr_scene_node_set_position(&second->node, 6, 6);
	struct wlr_scene_border *border = wlr_scene_border_create(windows, white, white);
	wlr_scene_border_set_geometry(border, 11, 10, 1, 0,
		(struct clipped_region){.area = {1, 1, 9, 8}},
		(struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 1, 2);
	wlr_scene_set_effect_light_layer(scene, wlr_scene_tree_create(&scene->tree));
	// Top panels, then fullscreen, then pinned preserve the native strata order.
	struct wlr_scene_rect *panel = wlr_scene_rect_create(&scene->tree, 16, 2, white);
	struct wlr_scene_rect *fullscreen = wlr_scene_rect_create(&scene->tree, 4, 4, pink);
	wlr_scene_node_set_position(&fullscreen->node, 12, 0);
	struct wlr_scene_tree *pinned = wlr_scene_tree_create(&scene->tree);
	struct wlr_scene_rect *pin = wlr_scene_rect_create(pinned, 2, 2, green);
	wlr_scene_node_set_position(&pin->node, 13, 1);
	struct wlr_scene_rect *overlay = wlr_scene_rect_create(&scene->tree, 8, 16, white);
	wlr_scene_node_set_position(&overlay->node, 8, 0);
	struct fx_effect_shader *window_shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(c.a - c.rgb, c.a); }", "source-invert");
	struct fx_effect_shader *border_shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv) { return vec4(0.0, 0.0, 1.0, 1.0); }", "source-light");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
	struct fx_animation_parameters lighting = parameters;
	lighting.light = (struct fx_effect_light){.enabled = true, .spread = 3, .intensity = 2, .threshold = 0.1f};
	struct wlr_buffer *display = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_buffer *unfiltered = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	bool ok = check(content_ok && window_shader && border_shader && display && unfiltered, "source targets and effects");
	(void)panel;
	for (unsigned mode = 0; ok && mode < 5; mode++) {
		fprintf(stderr, "source mode=%u transform=%d scale=%.2f\n", mode, fixture->output->transform, fixture->output->scale);
		wlr_scene_node_set_enabled(&blur->node, mode >= 2);
		wlr_scene_node_set_enabled(&optimized->node, mode == 4);
		wlr_scene_blur_set_should_only_blur_bottom_layer(blur, mode == 4);
		if (mode == 4) {
			wlr_scene_optimized_blur_mark_dirty(optimized);
		}
		wlr_scene_node_set_animation(&first->node, FX_SLOT_WINDOW, mode >= 1 ? window_shader : NULL, &parameters);
		wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, mode >= 3 ? border_shader : NULL, &lighting);
		wlr_scene_node_set_enabled(&overlay->node, false);
		struct wlr_output_state state;
		struct wlr_buffer *native = fixture_render_scene(fixture, output, &state);
		ok &= check(native != NULL, "native desktop reference");
		unsigned samples = counter.count;
		ok &= check(samples > 0, "ordinary scene reports sampled client buffer");
		// Overlay visibility must not punch holes in the complete source canvas.
		wlr_scene_node_set_enabled(&overlay->node, true);
		pixman_region32_t original_visibility, original_damage;
		pixman_region32_init(&original_visibility);
		pixman_region32_init(&original_damage);
		pixman_region32_copy(&original_visibility, &first->node.visible);
		pixman_region32_copy(&original_damage, &output->pending_commit_damage);
		bool optimized_dirty = optimized->dirty;
		ok &= check(fx_scene_capture_range_for_test(output, &backdrop->node, &pinned->node, display, false), "display source capture");
		ok &= check(optimized->dirty == optimized_dirty, "source cache never changes native optimized blur dirtiness");
		ok &= check(counter.count == samples, "source capture does not repeat output_sample");
		ok &= check(pixman_region32_equal(&original_visibility, &first->node.visible) &&
			pixman_region32_equal(&original_damage, &output->pending_commit_damage),
			"source capture preserves native visibility and output damage");
		pixman_region32_fini(&original_visibility);
		pixman_region32_fini(&original_damage);
		ok &= native && compare(fixture, native, display, "identity source including occluded desktop");
		if (native) {
			wlr_buffer_unlock(native);
		}
		wlr_output_state_finish(&state);
		ok &= check(fx_scene_capture_range_for_test(output, &backdrop->node, &pinned->node, unfiltered, true), "unfiltered source capture");
		if (mode <= 1 && fixture->output->transform == WL_OUTPUT_TRANSFORM_NORMAL && fixture->output->scale == 1) {
			struct fx_scene_source_pair_for_test pair = {0};
			uint64_t bytes = fx_scene_source_frozen_pair_bytes(output, &backdrop->node, &pinned->node);
			ok &= check(bytes > 0 && !fx_scene_source_pair_capture_for_test(output, &backdrop->node,
				&pinned->node, bytes - 1, &pair) && !pair.display && !pair.unfiltered,
				"insufficient paired reservation allocates no retained source");
			ok &= check(!fx_scene_source_pair_capture_for_test(output, &pinned->node,
				&backdrop->node, bytes, &pair) && !pair.display && !pair.unfiltered,
				"failed paired acquisition rolls back both candidates");
			ok &= check(fx_scene_source_pair_capture_for_test(output, &backdrop->node, &pinned->node, bytes, &pair),
				"owned paired source acquisition succeeds atomically");
			ok &= check((pair.display == pair.unfiltered) == (mode == 0), "equivalent roles alias; excluded stages retain separate roles");
			ok &= pair.display && pair.unfiltered && compare(fixture, display, pair.display, "paired display role") &&
				compare(fixture, unfiltered, pair.unfiltered, "paired unfiltered role");
			fx_scene_source_pair_finish_for_test(&pair);
			ok &= check(!pair.display && !pair.unfiltered && !pair.reserved_bytes,
				"paired finish releases both roles");
		}
		// The ordinary no-window-effect composition is the unfiltered oracle.
		wlr_scene_node_set_enabled(&overlay->node, false);
		wlr_scene_node_set_animation(&first->node, FX_SLOT_WINDOW, NULL, NULL);
		native = fixture_render_scene(fixture, output, &state);
		ok &= native && compare(fixture, native, unfiltered, "unfiltered source preserves border and light");
		if (native) {
			wlr_buffer_unlock(native);
		}
		wlr_output_state_finish(&state);
	}
	// The pair is retained before the client/source tree disappears. A later
	// capture reads its own already-retained image, not the display substitute.
	uint8_t before[4], after[4];
	if (unfiltered) {
		ok &= fixture_read_pixel(fixture, unfiltered, 7, 7, before);
	}
	wl_list_remove(&counter.listener.link);
	wlr_scene_node_destroy(&scene->tree.node);
	if (unfiltered) {
		ok &= fixture_read_pixel(fixture, unfiltered, 7, 7, after) &&
			check(memcmp(before, after, 4) == 0, "retained source remains readable after source destruction");
	}
	fx_effect_shader_unref(window_shader);
	fx_effect_shader_unref(border_shader);
	if (display) {
		wlr_buffer_drop(display);
	}
	if (unfiltered) {
		wlr_buffer_drop(unfiltered);
	}
	return ok;
}

// FP16 sources are working-space images: ordinary sRGB nodes are decoded,
// authored extended values survive, and encoding happens only at the landing.
static float source_half(uint16_t h) {
	int exponent = (h >> 10) & 31;
	float value = exponent == 0 ? ldexpf(h & 1023, -24)
		: ldexpf(1.0f + (h & 1023) / 1024.0f, exponent - 15);
	return h & 0x8000 ? -value : value;
}

// GLES does not guarantee packed ten-bit readback. Sampling into a gamma-valued
// FP16 attachment retains every ten-bit step without an eight-bit conversion.
static bool read_ten_bit(struct fixture *fixture, struct wlr_buffer *buffer, uint16_t *pixels) {
	struct wlr_buffer *readback = create_output_buffer(fixture, DRM_FORMAT_ABGR16161616F, 16, 16);
	struct wlr_texture *texture = buffer ? wlr_texture_from_buffer(fixture->renderer, buffer) : NULL;
	struct wlr_render_pass *pass = readback ? wlr_renderer_begin_buffer_pass(fixture->renderer, readback, NULL) : NULL;
	bool ok = texture && pass;
	if (ok) {
		wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = texture,
			.dst_box = {.width = 16, .height = 16}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
		ok = wlr_render_pass_submit(pass) && read_buffer(fixture, readback, DRM_FORMAT_ABGR16161616F, 16*8, pixels);
	}
	if (texture) wlr_texture_destroy(texture);
	if (readback) wlr_buffer_drop(readback);
	return ok;
}

static bool test_unmanaged_ten_bit(struct fixture *fixture) {
	bool ok = true;
	unsigned tested = 0;
	const uint32_t formats[] = {DRM_FORMAT_XRGB2101010, DRM_FORMAT_XBGR2101010};
	for (unsigned f = 0; f < 2; f++) {
		struct wlr_buffer *probe = create_output_buffer(fixture, formats[f], 16, 16);
		if (!probe) { fprintf(stderr, "ten-bit native allocator unavailable for format %08x\n", formats[f]); continue; }
		wlr_buffer_drop(probe);
		tested++;
		struct wlr_output_state setup;
		wlr_output_state_init(&setup);
		wlr_output_state_set_render_format(&setup, formats[f]);
		wlr_output_state_set_transform(&setup, WL_OUTPUT_TRANSFORM_NORMAL);
		wlr_output_state_set_scale(&setup, 1);
		ok &= check(wlr_output_commit_state(fixture->output, &setup), "select unmanaged ten-bit output");
		wlr_output_state_finish(&setup);
		struct wlr_scene *scene = wlr_scene_create();
		struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
		const float grey[] = {0.5f, 0.5f, 0.5f, 1};
		struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 16, 16, grey);
		struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
			"vec4 window(vec2 uv) { return vec4((512.0 + floor(uv.x*16.0))/1023.0, umbriel_sample(uv).g, 0.25, 1.0); }", "ten-bit-source");
		struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1};
		wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, shader, &parameters);
		struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16, get_render_format(fixture, formats[f]));
		struct wlr_output_state native;
		wlr_output_state_init(&native);
		ok &= check(shader && swapchain && wlr_scene_output_build_state(output, &native,
			&(struct wlr_scene_output_state_options){.swapchain = swapchain}) && native.buffer, "native ten-bit reference");
		struct fx_scene_source_view view = {.first = &rect->node, .last = &rect->node,
			.extent = {0, 0, 16, 16}, .scale = 1};
		struct fx_scene_source_view_plan plan;
		ok &= check(fx_scene_source_view_plan_for_test(output, &view, &plan) && plan.floating_point && !plan.working_space
			&& fx_scene_source_floating_point(output) && !fx_scene_source_working_space(output), "ten-bit precision is independent of value encoding");
		struct fx_scene_source_pair_for_test pair = {0};
		ok &= check(fx_scene_source_view_pair_capture_for_test(output, &view, plan.total_bytes, &pair)
			&& pair.floating_point && !pair.working_space, "capture encoded FP16 ten-bit source pair");
		uint16_t half[16 * 16 * 4];
		if (pair.display) {
			ok &= check(read_buffer(fixture, pair.display, DRM_FORMAT_ABGR16161616F, 16 * 8, half), "read encoded FP16 source");
			for (unsigned x = 0; x < 16; x++) {
				ok &= check(fabsf(source_half(half[x*4]) - (512.0f+x)/1023.0f) < 0.0005f
					&& fabsf(source_half(half[x*4+1]) - 0.5f) < 0.0005f, "source retains ten-bit steps without decoding");
			}
		}
		struct wlr_buffer *mixed = fx_scene_buffer_create(fixture->renderer, fixture->allocator, 16, 16, true);
		struct fx_scene_target *target = fx_scene_target_create_with_color(fixture->renderer, mixed, false, false);
		struct wlr_texture *texture = pair.display ? wlr_texture_from_buffer(fixture->renderer, pair.display) : NULL;
		struct fx_scene_input input = {.texture = texture, .sample_matrix = (float[]){1,0,0,0,1,0,0,0,1}};
		ok &= check(target && texture && fx_scene_target_blend(target, &input, &input, 0.5f, false), "encoded FP16 landing composition");
		struct wlr_buffer *landing = create_output_buffer(fixture, formats[f], 16, 16);
		struct wlr_texture *composed = mixed ? wlr_texture_from_buffer(fixture->renderer, mixed) : NULL;
		struct wlr_render_pass *pass = landing ? wlr_renderer_begin_buffer_pass(fixture->renderer, landing, NULL) : NULL;
		if (composed && pass) {
			wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = composed,
				.dst_box = {.width = 16, .height = 16}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
			ok &= check(wlr_render_pass_submit(pass), "present encoded FP16 into native ten-bit output");
			uint16_t reference[16*16*4], result[16*16*4];
			ok &= check(read_ten_bit(fixture, native.buffer, reference)
				&& read_ten_bit(fixture, landing, result), "read native and retained ten-bit pixels");
			for (unsigned x = 0; x < 16; x++) {
				float expected = (512.0f+x)/1023.0f;
				ok &= check(fabsf(source_half(reference[x*4])-expected) <= 1.1f/1023
					&& fabsf(source_half(result[x*4])-expected) <= 1.1f/1023,
					"native and retained ten-bit pixels match independent ramp");
			}

		} else ok = false;
		if (composed) wlr_texture_destroy(composed);
		if (texture) wlr_texture_destroy(texture);
		fx_scene_target_destroy(target);
		if (mixed) wlr_buffer_drop(mixed);
		if (landing) wlr_buffer_drop(landing);
		fx_scene_source_pair_finish_for_test(&pair);
		wlr_output_state_finish(&native);
		if (swapchain) wlr_swapchain_destroy(swapchain);
		fx_effect_shader_unref(shader);
		wlr_scene_node_destroy(&scene->tree.node);
	}
	ok &= check(tested > 0, "at least one native ten-bit format exercised");
	struct wlr_output_state restore;
	wlr_output_state_init(&restore);
	wlr_output_state_set_render_format(&restore, DRM_FORMAT_XRGB8888);
	ok &= wlr_output_commit_state(fixture->output, &restore);
	wlr_output_state_finish(&restore);
	return ok;
}

static bool test_working_source(struct fixture *fixture) {
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);
	wlr_output_state_set_scale(&state, 1);
	bool ok = wlr_output_commit_state(fixture->output, &state);
	wlr_output_state_finish(&state);
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float grey[] = {0.5f, 0.5f, 0.5f, 1};
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 16, 16, grey);
	struct wlr_buffer *source = create_output_buffer(fixture, DRM_FORMAT_ABGR16161616F, 16, 16);
	struct wlr_buffer *landing = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(2.0, 0.5, 0.125, 1.0); }", "source-working");
	struct wlr_color_transform *encoding = wlr_color_transform_init_linear_to_inverse_eotf(
		WLR_COLOR_TRANSFER_FUNCTION_SRGB);
	ok &= check(source && landing && shader && encoding, "working-space source resources");
	uint16_t pixels[16 * 16 * 4];
	for (unsigned mode = 0; ok && mode < 2; mode++) {
		struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
		wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, mode ? shader : NULL, &parameters);
		ok &= check(fx_scene_capture_range_for_test(output, &rect->node, &rect->node, source, false),
			"capture linear working source");
		ok &= check(read_buffer(fixture, source, DRM_FORMAT_ABGR16161616F, 16 * 8, pixels),
			"read working FP16 source");
		float expected[] = {mode ? 2.0f : 0.214041f, mode ? 0.5f : 0.214041f,
			mode ? 0.125f : 0.214041f, 1};
		for (unsigned c = 0; c < 4; c++) {
			if (!check(fabsf(source_half(pixels[c]) - expected[c]) < 0.002f,
					"working source preserves linear and extended channel")) {
				fprintf(stderr, "mode=%u channel=%u actual=%f expected=%f\n", mode, c,
					source_half(pixels[c]), expected[c]);
				ok = false;
			}
		}
		struct wlr_texture *texture = wlr_texture_from_buffer(fixture->renderer, source);
		struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, landing,
			&(struct wlr_buffer_pass_options){.color_transform = encoding});
		if (!texture || !pass) {
			ok = false;
		} else {
			wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = texture,
				.dst_box = {.width = 16, .height = 16}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
				.transfer_function = WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR});
			ok &= check(wlr_render_pass_submit(pass), "encode source at final landing");
			uint8_t pixel[4];
			ok &= fixture_read_pixel(fixture, landing, 0, 0, pixel);
			const int expected_bgr[2][3] = {{128, 128, 128}, {99, 188, 255}};
			for (unsigned c = 0; c < 3; c++) {
				ok &= check(abs(pixel[c] - expected_bgr[mode][c]) <= 1, "working source encoded exactly once");
			}
		}
		if (texture) {
			wlr_texture_destroy(texture);
		}
	}
	// Managed outputs acquire FP16 pairs; the helper never silently quantizes
	// the working source back into an eight-bit snapshot.
	output->combined_color_transform = wlr_color_transform_ref(encoding);
	struct fx_scene_source_pair_for_test pair = {0};
	uint64_t pair_bytes = fx_scene_source_pair_bytes_for_test(output);
	ok &= check(pair_bytes > 0 && fx_scene_source_pair_capture_for_test(output,
		&rect->node, &rect->node, pair_bytes, &pair) && pair.working_space,
		"paired helper preserves managed working format");
	if (pair.display) {
		ok &= check(read_buffer(fixture, pair.display, DRM_FORMAT_ABGR16161616F, 16 * 8, pixels)
			&& fabsf(source_half(pixels[0]) - 2.0f) < 0.002f, "paired helper retains extended values");
	}
	fx_scene_source_pair_finish_for_test(&pair);
	wlr_color_transform_unref(encoding);
	fx_effect_shader_unref(shader);
	if (source) wlr_buffer_drop(source);
	if (landing) wlr_buffer_drop(landing);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static bool source_native_feedback(struct fixture *fixture, struct wlr_scene_output *output) {
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16,
		get_render_format(fixture, DRM_FORMAT_ARGB8888));
	if (!swapchain) return false;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_scene_output_damage_whole_for_test(output);
	bool ok = wlr_scene_output_build_state(output, &state,
		&(struct wlr_scene_output_state_options){.swapchain = swapchain, .effect_capture_pending = true});
	wlr_output_state_finish(&state);
	wlr_swapchain_destroy(swapchain);
	return ok;
}

static bool test_source_history(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float blue[] = {0, 0, 1, 1};
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 16, 16, blue);
	struct fx_effect_shader *accumulate = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { vec4 p = umbriel_sample_previous(uv); "
		"return vec4(min(p.r + 0.25, 1.0), p.b, umbriel_sample(uv).b, 1.0); }", "source-history");
	struct fx_effect_shader *green = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }", "source-history-green");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, green, &parameters);
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOWS_IN, accumulate, &parameters);
	wlr_scene_output_set_effect_capture_policy(output, false);
	struct wlr_buffer *display = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_buffer *capture = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	bool ok = check(accumulate && green && display && capture, "source history fixture resources");
	ok &= check(source_native_feedback(fixture, output), "seed both native role histories");
	uint64_t bytes = fx_scene_source_session_bytes_for_test(output, &rect->node, &rect->node);
	ok &= check(bytes > 0 && bytes == fx_scene_source_pair_bytes_for_test(output), "copied histories included in reservation");
	ok &= check(!fx_scene_source_session_create_for_test(output, &rect->node, &rect->node, bytes - 1),
		"history acquisition rejects insufficient reservation");
	struct fx_scene_source_session *session = fx_scene_source_session_create_for_test(
		output, &rect->node, &rect->node, bytes);
	ok &= check(session != NULL, "snapshot paired native histories atomically");
	ok &= check(source_native_feedback(fixture, output), "native history promotes after acquisition");
	struct fx_scene_source_pair_for_test frozen = {0};
	uint64_t frozen_bytes = fx_scene_source_pair_bytes_for_test(output);
	ok &= check(fx_scene_source_pair_capture_for_test(output, &rect->node, &rect->node,
		frozen_bytes, &frozen), "freeze already rendered feedback stage");
	if (frozen.display && frozen.unfiltered) {
		uint8_t shown[4], plain[4];
		ok &= fixture_read_pixel(fixture, frozen.display, 8, 8, shown)
			&& fixture_read_pixel(fixture, frozen.unfiltered, 8, 8, plain);
		ok &= check(abs(shown[2] - 128) <= 2 && abs(plain[2] - 128) <= 2,
			"frozen acquisition replays current result without extra feedback step");
	}
	fx_scene_source_pair_finish_for_test(&frozen);

	for (unsigned frame = 0; ok && frame < 3; frame++) {
		ok &= check(fx_scene_source_session_begin_frame_for_test(session), "begin source history frame");
		ok &= check(fx_scene_source_session_capture_for_test(session, display, false), "render isolated display history");
		uint8_t before[4], after[4], plain[4];
		ok &= fixture_read_pixel(fixture, display, 8, 8, before);
		ok &= check(fx_scene_source_session_capture_for_test(session, display, false), "same instant replays retained image");
		ok &= fixture_read_pixel(fixture, display, 8, 8, after);
		ok &= check(!memcmp(before, after, 4), "replay does not advance feedback");
		// A native promotion between role captures cannot alter the pinned role.
		ok &= check(source_native_feedback(fixture, output), "interleaved native promotion");
		ok &= check(fx_scene_source_session_capture_for_test(session, capture, true), "render isolated capture history");
		ok &= fixture_read_pixel(fixture, capture, 8, 8, plain);
		int red = frame < 2 ? 128 : 192;
		ok &= check(abs(before[2] - red) <= 2 && abs(plain[2] - red) <= 2,
			"source history advances only after successful final output submit");
		ok &= check(before[0] < 2 && before[1] < 2 && plain[0] > 253 && plain[1] > 253,
			"source display and capture histories stay independent");
		fx_scene_source_session_finish_frame_for_test(session, frame != 0);
	}
	wlr_scene_node_destroy(&scene->tree.node);
	ok &= check(!fx_scene_source_session_begin_frame_for_test(session), "source destruction invalidates session safely");
	fx_scene_source_session_destroy_for_test(session);
	fx_effect_shader_unref(accumulate);
	fx_effect_shader_unref(green);
	if (display) wlr_buffer_drop(display);
	if (capture) wlr_buffer_drop(capture);
	return ok;
}

static bool test_frozen_replacement_roles(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_tree *desktop = wlr_scene_tree_create(&scene->tree);
	const float blue[] = {0, 0, 1, 1};
	struct wlr_scene_rect *client = wlr_scene_rect_create(desktop, 16, 16, blue);
	struct fx_effect_shader *green = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }", "frozen-role-green");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
	wlr_scene_node_set_animation(&client->node, FX_SLOT_WINDOW, green, &parameters);
	struct fx_scene_source_pair_for_test pair = {0};
	uint64_t bytes = fx_scene_source_pair_bytes_for_test(output);
	bool ok = check(green && fx_scene_source_pair_capture_for_test(output,
		&desktop->node, &desktop->node, bytes, &pair), "acquire distinct frozen presentation roles");
	struct wlr_scene_buffer *picture = pair.display ? wlr_scene_buffer_create(&scene->tree, pair.display) : NULL;
	ok &= check(picture && fx_scene_output_replace_range_for_test(output, &desktop->node, &desktop->node)
		&& fx_scene_output_bind_replacement_roles_for_test(output, picture, pair.unfiltered),
		"bind frozen replacement capture role");
	wlr_scene_output_set_effect_capture_policy(output, false);
	// The original client and its filter disappear. Capture still gets the
	// already retained unfiltered role, with no live filter to trigger a pass.
	wlr_scene_node_destroy(&client->node);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16,
		get_render_format(fixture, DRM_FORMAT_ARGB8888));
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_scene_output_damage_whole_for_test(output);
	ok &= check(swapchain && wlr_scene_output_build_state(output, &state,
		&(struct wlr_scene_output_state_options){.swapchain = swapchain, .effect_capture_pending = true})
		&& state.buffer, "capture retained replacement after client destruction");
	if (state.buffer) {
		uint8_t display[4], capture[4];
		ok &= fixture_read_display_pixel(fixture, state.buffer, 8, 8, display)
			&& fixture_read_pixel(fixture, state.buffer, 8, 8, capture);
		ok &= check(display[1] > 253 && display[0] < 2 && display[2] < 2,
			"frozen display keeps selected window effect");
		ok &= check(capture[0] > 253 && capture[1] < 2 && capture[2] < 2,
			"mid-frozen capture uses retained unfiltered role");
	}
	wlr_output_state_finish(&state);
	if (swapchain) wlr_swapchain_destroy(swapchain);
	fx_scene_output_replace_range_for_test(output, NULL, NULL);
	wlr_scene_node_destroy(&scene->tree.node);
	fx_scene_source_pair_finish_for_test(&pair);
	fx_effect_shader_unref(green);
	return ok;
}

static bool test_mirrored_replacement(struct fixture *fixture) {
	struct wlr_output *other = wlr_headless_add_output(fixture->backend, 16, 16);
	bool ok = other && wlr_output_init_render(other, fixture->allocator, fixture->renderer);
	if (!ok) return false;
	struct wlr_output_state enabled;
	wlr_output_state_init(&enabled);
	wlr_output_state_set_enabled(&enabled, true);
	ok &= wlr_output_commit_state(other, &enabled);
	wlr_output_state_finish(&enabled);
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *owner = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_output *peer = wlr_scene_output_create(scene, other);
	struct wlr_scene_tree *desktop = wlr_scene_tree_create(&scene->tree);
	const float blue[] = {0, 0, 1, 1};
	struct wlr_scene_rect *client = wlr_scene_rect_create(desktop, 16, 16, blue);
	struct wlr_buffer *green = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_buffer *clean = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_buffer *native_source = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	ok &= green && clean && native_source;
	if (!ok) return false;
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, green, NULL);
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, 16, 16}, .color = {0, 1, 0, 1}});
	ok &= wlr_render_pass_submit(pass);
	ok &= fx_scene_capture_range_for_test(owner, &desktop->node, &desktop->node, clean, false);
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	pixman_region32_copy(&visible, &client->node.visible);
	struct wlr_scene_tree *presentation = wlr_scene_tree_create(&scene->tree);
	// The full output picture and its nonopaque-declared backing are one output
	// presentation subtree. Neither may cover a mirrored native output.
	wlr_scene_buffer_create(presentation, green);
	struct wlr_scene_buffer *picture = wlr_scene_buffer_create(presentation, green);
	ok &= check(fx_scene_output_replace_range_for_test(owner, &desktop->node, &desktop->node)
		&& fx_scene_output_bind_replacement_roles_for_test(owner, picture, clean), "bind mirrored output-local presentation");
	ok &= check(pixman_region32_equal(&visible, &client->node.visible), "presentation preserves native global visibility");
	struct sample_counter samples = {.listener.notify = sampled};
	wl_signal_add(&picture->events.output_sample, &samples.listener);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16, get_render_format(fixture, DRM_FORMAT_ARGB8888));
	for (unsigned index = 0; ok && index < 2; index++) {
		struct wlr_scene_output *output = index ? peer : owner;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_scene_output_damage_whole_for_test(output);
		ok &= wlr_scene_output_build_state(output, &state, &(struct wlr_scene_output_state_options){.swapchain = swapchain});
		uint8_t pixel[4];
		ok &= state.buffer && fixture_read_display_pixel(fixture, state.buffer, 8, 8, pixel);
		ok &= check(index ? pixel[0] > 253 && pixel[1] < 2 : pixel[1] > 253 && pixel[0] < 2,
			"mirrored peer renders native blue while owning output presents green");
		wlr_output_state_finish(&state);
	}
	ok &= check(samples.count == 1, "foreign output emits no sample callback for owned picture");
	pixman_region32_clear(&peer->pending_commit_damage);
	wlr_scene_buffer_set_buffer(picture, clean);
	wlr_scene_node_set_position(&presentation->node, 1, 0);
	ok &= check(!pixman_region32_not_empty(&peer->pending_commit_damage), "owned picture buffer and geometry updates do not damage mirrored peer");
	ok &= check(fx_scene_capture_range_for_test(peer, &desktop->node, &presentation->node, native_source, false),
		"independent mirrored source excludes presentation subtree");
	uint8_t source_pixel[4];
	ok &= fixture_read_pixel(fixture, native_source, 8, 8, source_pixel)
		&& check(source_pixel[0] > 253 && source_pixel[1] < 2, "source contains native content instead of recursive presentation");
	wl_list_remove(&samples.listener.link);
	pixman_region32_fini(&visible);
	wlr_swapchain_destroy(swapchain);
	wlr_scene_node_destroy(&presentation->node);
	ok &= check(!pixman_region32_not_empty(&peer->pending_commit_damage), "owned subtree destruction does not damage mirrored peer");
	ok &= check(scene->source_replacement_count_for_test == 0 && owner->source_replacement_for_test == NULL,
		"presentation subtree destruction clears output owner safely");
	wlr_scene_node_destroy(&scene->tree.node);
	wlr_buffer_drop(green);
	wlr_buffer_drop(clean);
	wlr_buffer_drop(native_source);
	wlr_output_destroy(other);
	return ok;
}

static bool test_rectangular_source(struct fixture *fixture) {
	bool ok = true;
	for (unsigned scale = 0; ok && scale < 2; scale++) {
		for (enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
				ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
			struct wlr_output_state state;
			wlr_output_state_init(&state);
			wlr_output_state_set_enabled(&state, true);
			wlr_output_state_set_custom_mode(&state, 32, 16, 60000);
			wlr_output_state_set_transform(&state, transform);
			wlr_output_state_set_scale(&state, scale ? 1.25f : 1);
			ok &= check(wlr_output_commit_state(fixture->output, &state), "rectangular source output mode");
			wlr_output_state_finish(&state);
			if (!ok) break;
			struct wlr_scene *scene = wlr_scene_create();
			struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
			wlr_scene_output_set_position(output, -12, 8);
			wlr_scene_node_set_position(&scene->tree.node, -12, 8);
			struct wlr_scene_tree *desktop = wlr_scene_tree_create(&scene->tree);
			const float back[] = {0.2f, 0.3f, 0.4f, 1}, red[] = {1, 0, 0, 1};
			wlr_scene_rect_create(desktop, 40, 40, back);
			struct wlr_scene_tree *clipped = wlr_scene_tree_create(desktop);
			wlr_scene_tree_set_clip(clipped, &(struct wlr_box){1, 2, 7, 9});
			wlr_scene_rect_create(clipped, 5, 6, red);
			struct wlr_scene_tree *disabled = wlr_scene_tree_create(desktop);
			wlr_scene_rect_create(disabled, 40, 40, red);
			wlr_scene_node_set_enabled(&disabled->node, false);
			struct wlr_buffer *source = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 32, 16);
			struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 32, 16,
				get_render_format(fixture, DRM_FORMAT_ARGB8888));
			wlr_output_state_init(&state);
			ok &= check(source && swapchain && wlr_scene_output_build_state(output, &state,
				&(struct wlr_scene_output_state_options){.swapchain = swapchain}) && state.buffer,
				"rectangular native reference");
			ok &= check(fx_scene_capture_range_for_test(output, &desktop->node, &desktop->node, source, false),
				"rectangular source capture");
			uint8_t native[32 * 16 * 4], captured[sizeof(native)];
			if (state.buffer && source) {
				ok &= check(read_buffer(fixture, state.buffer, DRM_FORMAT_ARGB8888, 32 * 4, native)
					&& read_buffer(fixture, source, DRM_FORMAT_ARGB8888, 32 * 4, captured), "rectangular source readback");
				ok &= check(!memcmp(native, captured, sizeof(native)), "rectangular source matches native under transform/scale");
			}
			wlr_output_state_finish(&state);
			if (swapchain) wlr_swapchain_destroy(swapchain);
			if (source) wlr_buffer_drop(source);
			wlr_scene_node_destroy(&scene->tree.node);
		}
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_custom_mode(&state, 16, 16, 60000);
	ok &= wlr_output_commit_state(fixture->output, &state);
	wlr_output_state_finish(&state);
	return ok;
}

static bool test_working_replacement(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_tree *desktop = wlr_scene_tree_create(&scene->tree);
	const float grey[] = {0.5f, 0.5f, 0.5f, 1};
	struct wlr_scene_rect *client = wlr_scene_rect_create(desktop, 16, 16, grey);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(2.0, 0.5, 0.125, 1.0); }", "linear-replacement");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
	wlr_scene_node_set_animation(&client->node, FX_SLOT_WINDOW, shader, &parameters);
	struct wlr_color_transform *encoding = wlr_color_transform_init_linear_to_inverse_eotf(
		WLR_COLOR_TRANSFER_FUNCTION_SRGB);
	output->combined_color_transform = wlr_color_transform_ref(encoding);
	struct fx_scene_source_pair_for_test pair = {0};
	uint64_t bytes = fx_scene_source_pair_bytes_for_test(output);
	bool ok = check(shader && encoding && bytes && fx_scene_source_pair_capture_for_test(output,
		&desktop->node, &desktop->node, bytes, &pair) && pair.working_space,
		"capture working-space replacement pair");
	struct wlr_scene_buffer *picture = pair.display ? wlr_scene_buffer_create(&scene->tree, pair.display) : NULL;
	if (picture) {
		wlr_scene_buffer_set_transfer_function(picture, WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR);
		wlr_scene_buffer_set_primaries(picture, WLR_COLOR_NAMED_PRIMARIES_SRGB);
	}
	ok &= check(picture && fx_scene_output_replace_range_for_test(output, &desktop->node, &desktop->node)
		&& fx_scene_output_bind_replacement_roles_for_test(output, picture, pair.unfiltered),
		"bind linear scene-buffer replacement");
	wlr_scene_output_set_effect_capture_policy(output, false);
	wlr_scene_node_destroy(&client->node);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16,
		get_render_format(fixture, DRM_FORMAT_ARGB8888));
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_scene_output_damage_whole_for_test(output);
	ok &= check(swapchain && wlr_scene_output_build_state(output, &state,
		&(struct wlr_scene_output_state_options){.swapchain = swapchain, .effect_capture_pending = true,
			.color_transform = encoding}) && state.buffer, "present both retained linear roles through native scene");
	if (state.buffer) {
		uint8_t display[4], capture[4];
		ok &= fixture_read_display_pixel(fixture, state.buffer, 8, 8, display)
			&& fixture_read_pixel(fixture, state.buffer, 8, 8, capture);
		const int expected[] = {99, 188, 255};
		for (unsigned c = 0; c < 3; c++) {
			ok &= check(abs(display[c] - expected[c]) <= 1, "native display encodes linear replacement exactly once");
			ok &= check(abs(capture[c] - 128) <= 1, "native unfiltered capture encodes linear replacement exactly once");
		}
	}
	wlr_output_state_finish(&state);
	if (swapchain) wlr_swapchain_destroy(swapchain);
	fx_scene_output_replace_range_for_test(output, NULL, NULL);
	fx_scene_source_pair_finish_for_test(&pair);
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(shader);
	wlr_color_transform_unref(encoding);
	return ok;
}

static bool test_frozen_feedback_light(struct fixture *fixture, bool split) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float grey[] = {0.15f, 0.2f, 0.25f, 1}, white[] = {1, 1, 1, 1};
	struct wlr_scene_rect *background = wlr_scene_rect_create(&scene->tree, 16, 16, grey);
	struct wlr_scene_border *border = wlr_scene_border_create(&scene->tree, white, white);
	wlr_scene_border_set_geometry(border, 8, 8, 1, 0, (struct clipped_region){.area = {1, 1, 6, 6}},
		(struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 4, 4);
	struct wlr_scene_tree *lights = wlr_scene_tree_create(&scene->tree);
	wlr_scene_set_effect_light_layer(scene, lights);
	struct fx_effect_shader *feedback = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv){vec4 p=umbriel_sample_previous(uv);return vec4(p.r*0.5,0.3,0.1,1.0);}",
		"frozen-feedback-light");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1,
		.light = {.enabled = true, .spread = 2, .intensity = 2, .threshold = 0.05f}};
	struct fx_effect_shader *identity = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv){return umbriel_sample(uv);}", "feedback-light-role-trigger");
	wlr_scene_node_set_animation(&background->node, FX_SLOT_WINDOW, split ? identity : NULL, &parameters);
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, feedback, &parameters);
	wlr_scene_output_set_effect_capture_policy(output, false);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16,
		get_render_format(fixture, DRM_FORMAT_ARGB8888));
	bool ok = check(feedback && identity && swapchain, "feedback light resources");
	ok &= check(fx_scene_source_pair_bytes_for_test(output) == 0,
		"feedback light without represented emission explicitly declines freeze");
	for (unsigned frame = 0; ok && frame < 2; frame++) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_scene_output_damage_whole_for_test(output);
		ok &= check(wlr_scene_output_build_state(output, &state,
			&(struct wlr_scene_output_state_options){.swapchain = swapchain, .effect_capture_pending = true})
			&& state.buffer, "present native feedback border and its emission");
		ok &= check(fx_scene_source_pair_bytes_for_test(output) == 0,
			"uncommitted feedback emission cannot be acquired as a displayed freeze");
		ok &= check(fx_scene_emission_source_bytes(output, &border->node) == 0,
			"raw emission export rejects in-flight native frame");
		ok &= check(wlr_output_commit_state(fixture->output, &state), "commit feedback emission before freeze admission");
		struct fx_scene_emission_source emission = {0};
		uint64_t emission_bytes = fx_scene_emission_source_bytes(output, &border->node);
		ok &= check(emission_bytes && !fx_scene_emission_source_capture(output, &border->node, emission_bytes - 1, &emission)
			&& emission.display == NULL, "raw emission reservation is atomic");
		ok &= check(fx_scene_emission_source_capture(output, &border->node, emission_bytes, &emission)
			&& (emission.display == emission.unfiltered) == !split && !emission.working_space
			&& emission.recipe.intensity == 2 && emission.recipe.spread == 2,
			"retain committed raw role emission without another feedback evaluation");
		for (unsigned role = 0; ok && role < 2; role++) {
			struct wlr_buffer *image = role ? emission.unfiltered : emission.display;
			struct fx_framebuffer *fb = fx_framebuffer_get_or_create(fx_get_renderer(fixture->renderer), image);
			unsigned count = image->width * image->height * 4;
			float max_red = 0, max_green = 0;
			if (fb->drm_format == DRM_FORMAT_ABGR16161616F) {
				uint16_t pixels[count];
				ok &= read_buffer(fixture, image, DRM_FORMAT_ABGR16161616F, image->width * 8, pixels);
				for (unsigned i = 0; i < count; i += 4) {
					max_red = fmaxf(max_red, source_half(pixels[i]));
					max_green = fmaxf(max_green, source_half(pixels[i + 1]));
				}
			} else {
				uint8_t pixels[count];
				ok &= read_buffer(fixture, image, DRM_FORMAT_ABGR8888, image->width * 4, pixels);
				for (unsigned i = 0; i < count; i += 4) {
					max_red = fmaxf(max_red, pixels[i] / 255.0f);
					max_green = fmaxf(max_green, pixels[i + 1] / 255.0f);
				}
			}
			if (!check(fabsf(max_red - (frame ? 0.25f : 0.5f)) < 0.01f && fabsf(max_green - 0.3f) < 0.01f,
					"raw emission preserves represented feedback step before threshold/blur")) {
				fprintf(stderr, "frame%u role%u rawmax=%f,%f\n", frame, role, max_red, max_green);
				ok = false;
			}
		}
		fx_scene_emission_source_finish(&emission);
		struct fx_scene_source_pair_for_test pair = {0};
		uint64_t bytes = fx_scene_source_pair_bytes_for_test(output);
		ok &= check(bytes && fx_scene_source_pair_capture_for_test(output,
			&background->node, &lights->node, bytes, &pair), "freeze completed border history and exact emission");
		ok &= check((pair.display == pair.unfiltered) == !split,
			"border-only native capture aliases displayed history; excluded stages retain independent roles");
		if (state.buffer && pair.display && pair.unfiltered) {
			for (int y = 0; ok && y < 16; y++) {
				for (int x = 0; ok && x < 16; x++) {
					uint8_t native[4], captured[4], frozen[4], plain[4];
					ok &= fixture_read_display_pixel(fixture, state.buffer, x, y, native)
						&& fixture_read_pixel(fixture, state.buffer, x, y, captured)
						&& fixture_read_pixel(fixture, pair.display, x, y, frozen)
						&& fixture_read_pixel(fixture, pair.unfiltered, x, y, plain);
					for (unsigned c = 0; c < 4; c++) {
						if (abs(native[c] - captured[c]) > 1 || abs(native[c] - frozen[c]) > 1 || abs(captured[c] - plain[c]) > 1) {
							fprintf(stderr, "feedback light frame%u (%d,%d) channel%u display%u/%u capture%u/%u\n",
								frame, x, y, c, native[c], frozen[c], captured[c], plain[c]);
							ok = false;
						}
					}
				}
			}
		}
		fx_scene_source_pair_finish_for_test(&pair);
		wlr_output_state_finish(&state);
	}
	if (swapchain) wlr_swapchain_destroy(swapchain);
	fx_effect_shader_unref(feedback);
	fx_effect_shader_unref(identity);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static bool test_working_history(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float blue[] = {0, 0, 1, 1};
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 16, 16, blue);
	struct fx_effect_shader *history = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { vec4 p=umbriel_sample_previous(uv); return vec4(p.r+2.0,0.0,1.0,1.0); }",
		"source-working-history");
	struct fx_effect_shader *identity = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return umbriel_sample(uv); }", "source-working-identity");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOWS_IN, history, &parameters);
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, identity, &parameters);
	wlr_scene_output_set_effect_capture_policy(output, false);
	struct wlr_color_transform *encoding = wlr_color_transform_init_linear_to_inverse_eotf(
		WLR_COLOR_TRANSFER_FUNCTION_SRGB);
	struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 16, 16,
		get_render_format(fixture, DRM_FORMAT_ARGB8888));
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool ok = check(history && identity && encoding && swapchain && wlr_scene_output_build_state(output,
		&state, &(struct wlr_scene_output_state_options){.swapchain = swapchain,
			.color_transform = encoding, .effect_capture_pending = true}), "seed managed role histories");
	wlr_output_state_finish(&state);
	struct fx_scene_source_pair_for_test pair = {0};
	uint64_t bytes = fx_scene_source_pair_bytes_for_test(output);
	ok &= check(bytes > 0 && fx_scene_source_pair_capture_for_test(output, &rect->node, &rect->node, bytes, &pair),
		"freeze FP16 feedback roles");
	uint16_t pixels[16 * 16 * 4];
	if (pair.display && pair.unfiltered) {
		ok &= check(pair.working_space && read_buffer(fixture, pair.display, DRM_FORMAT_ABGR16161616F, 16 * 8, pixels)
			&& fabsf(source_half(pixels[0]) - 2) < 0.002f, "frozen feedback copy preserves extended FP16 value");
		ok &= check(read_buffer(fixture, pair.unfiltered, DRM_FORMAT_ABGR16161616F, 16 * 8, pixels)
			&& fabsf(source_half(pixels[0]) - 2) < 0.002f, "frozen capture feedback retains own FP16 value");
	}
	fx_scene_source_pair_finish_for_test(&pair);
	struct fx_scene_source_session *session = fx_scene_source_session_create_for_test(output,
		&rect->node, &rect->node, fx_scene_source_session_bytes_for_test(output, &rect->node, &rect->node));
	struct wlr_buffer *target = create_output_buffer(fixture, DRM_FORMAT_ABGR16161616F, 16, 16);
	ok &= check(session && target && fx_scene_source_session_begin_frame_for_test(session)
		&& fx_scene_source_session_capture_for_test(session, target, false), "advance isolated FP16 feedback source");
	if (target) {
		ok &= check(read_buffer(fixture, target, DRM_FORMAT_ABGR16161616F, 16 * 8, pixels)
			&& fabsf(source_half(pixels[0]) - 4) < 0.002f, "cloned FP16 history is raw working data");
	}
	fx_scene_source_session_finish_frame_for_test(session, false);
	fx_scene_source_session_destroy_for_test(session);
	if (target) wlr_buffer_drop(target);
	if (swapchain) wlr_swapchain_destroy(swapchain);
	wlr_color_transform_unref(encoding);
	fx_effect_shader_unref(history);
	fx_effect_shader_unref(identity);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static bool test_source_light_isolation(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float white[] = {1, 1, 1, 1};
	struct wlr_scene_border *border = wlr_scene_border_create(&scene->tree, white, white);
	wlr_scene_border_set_geometry(border, 8, 8, 1, 0,
		(struct clipped_region){.area = {1, 1, 6, 6}}, (struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 4, 4);
	struct wlr_scene_tree *lights = wlr_scene_tree_create(&scene->tree);
	wlr_scene_set_effect_light_layer(scene, lights);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"uniform float gain; vec4 border(vec2 uv) { return vec4(0.0,0.0,gain,1.0); }", "source-private-light");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1,
		.light = {.enabled = true, .spread = 2, .intensity = 2, .threshold = 0.05f}};
	struct fx_uniform *gain = fx_parameters_add_uniform(&parameters, "gain", FX_UNIFORM_FLOAT, 1);
	gain->floats[0] = 0.2f;
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, shader, &parameters);
	struct wlr_output_state state;
	struct wlr_buffer *native = fixture_render_scene(fixture, output, &state);
	bool ok = check(shader && native, "seed native border emission");
	if (native) wlr_buffer_unlock(native);
	wlr_output_state_finish(&state);
	struct wlr_buffer *before = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_buffer *after = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_buffer *source = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	ok &= check(before && after && source && fx_scene_capture_participant_for_test(output, &lights->node, before),
		"retain native emission before independent source");
	gain->floats[0] = 0.8f;
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, shader, &parameters);
	ok &= check(fx_scene_capture_range_for_test(output, &border->node, &lights->node, source, false),
		"source computes its own different emission");
	ok &= check(fx_scene_capture_participant_for_test(output, &lights->node, after), "native emission remains available");
	ok &= before && after && compare(fixture, before, after, "source capture never overwrites native emission cache");
	uint8_t pixels[16 * 16 * 4];
	if (before && read_buffer(fixture, before, DRM_FORMAT_ARGB8888, 16 * 4, pixels)) {
		unsigned sum = 0;
		for (unsigned i = 0; i < sizeof(pixels); i += 4) sum += pixels[i];
		ok &= check(sum > 0, "emission preservation check has visible light");
	} else {
		ok = false;
	}
	if (before) wlr_buffer_drop(before);
	if (after) wlr_buffer_drop(after);
	if (source) wlr_buffer_drop(source);
	fx_effect_shader_unref(shader);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static bool test_frozen_output_locality(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float blue[] = {0,0,1,1}, white[] = {1,1,1,1};
	struct wlr_scene_rect *local = wlr_scene_rect_create(&scene->tree,16,16,blue);
	struct wlr_scene_tree *foreign = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_set_position(&foreign->node,1000,0);
	struct wlr_scene_border *border = wlr_scene_border_create(foreign,white,white);
	wlr_scene_border_set_geometry(border,16,16,2,0,(struct clipped_region){0},
		(struct fx_corner_radii){0},(struct fx_corner_radii){0});
	struct wlr_scene_tree *lights = wlr_scene_tree_create(&scene->tree);
	wlr_scene_set_effect_light_layer(scene,lights);
	struct fx_effect_shader *feedback = fx_effect_shader_create(fixture->renderer,FX_EFFECT_BORDER,
		"vec4 border(vec2 uv){return umbriel_sample_previous(uv)*0.5;}","foreign-frozen-feedback");
	struct fx_animation_parameters parameters = {.progress=1,.linear_progress=1,.direction=1,
		.light={.enabled=true,.spread=2,.intensity=1,.threshold=0}};
	wlr_scene_node_set_animation(&border->node,FX_SLOT_BORDER_EFFECT,feedback,&parameters);
	uint64_t bytes = fx_scene_source_frozen_pair_bytes(output,&local->node,&lights->node);
	struct fx_scene_source_pair_for_test pair={0};
	bool ok = check(feedback && bytes && fx_scene_source_pair_capture_for_test(output,&local->node,&lights->node,bytes,&pair),
		"foreign uncommitted feedback light does not reject local native freeze");
	if(pair.display) {
		uint8_t pixel[4]; ok &= fixture_read_pixel(fixture,pair.display,8,8,pixel);
		ok &= check(pixel[0]>253 && pixel[1]<2 && pixel[2]<2,"local frozen pixels exclude foreign output stage");
	}
	fx_scene_source_pair_finish_for_test(&pair);
	wlr_scene_node_set_position(&foreign->node,0,0);
	ok &= check(fx_scene_source_frozen_pair_bytes(output,&local->node,&lights->node)==0,
		"same uncommitted feedback light reaching this output correctly rejects freeze");
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(feedback);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fixture_finish(&fixture);
		return 77;
	}
	bool locality_ok = test_frozen_output_locality(&fixture);
	bool ok = locality_ok && true;
	const float scales[] = {1, 1.25f};
	for (unsigned scale = 0; ok && scale < sizeof(scales) / sizeof(scales[0]); scale++) {
		for (enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
				ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
			struct wlr_output_state state;
			wlr_output_state_init(&state);
			wlr_output_state_set_transform(&state, transform);
			wlr_output_state_set_scale(&state, scales[scale]);
			ok &= check(wlr_output_commit_state(fixture.output, &state), "set source experiment output transform");
			wlr_output_state_finish(&state);
			ok &= test_source(&fixture);
		}
	}
	ok &= test_rectangular_source(&fixture);
	ok &= test_working_source(&fixture);
	ok &= test_unmanaged_ten_bit(&fixture);
	ok &= test_source_history(&fixture);
	ok &= test_working_history(&fixture);
	ok &= test_working_replacement(&fixture);
	ok &= test_frozen_feedback_light(&fixture, false);
	ok &= test_frozen_feedback_light(&fixture, true);
	ok &= test_source_light_isolation(&fixture);
	ok &= test_frozen_replacement_roles(&fixture);
	ok &= test_mirrored_replacement(&fixture);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
