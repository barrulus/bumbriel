// C0 virtual workspace source: no native enable/position/clip mutation.
#include "render_fixture.h"
#include "types/scene_source.h"
#include "types/wlr_scene.h"
#include "render/fx_renderer/scene_resources.h"
#include "umbrielfx/render/effect.h"

struct samples {
	struct wl_listener listener;
	unsigned count;
};
static void sampled(struct wl_listener *listener, void *data) {
	struct samples *samples = wl_container_of(listener, samples, listener);
	(void)data;
	samples->count++;
}
static bool equal(struct fixture *fixture, struct wlr_buffer *native, struct wlr_buffer *source) {
	uint8_t a[16 * 16 * 4], b[sizeof(a)];
	if (!read_buffer(fixture, native, DRM_FORMAT_ARGB8888, 16 * 4, a)
			|| !read_buffer(fixture, source, DRM_FORMAT_ARGB8888, 16 * 4, b)) return false;
	for (unsigned i = 0; i < sizeof(a); i++) {
		if (abs(a[i] - b[i]) > 1) {
			fprintf(stderr, "virtual source byte %u native=%u source=%u\n", i, a[i], b[i]);
			return false;
		}
	}
	return true;
}

static bool experiment(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	wlr_scene_output_set_position(output, -8, 16);
	wlr_scene_node_set_position(&scene->tree.node, -8, 16);
	const float backdrop_color[] = {0.1f, 0.2f, 0.3f, 1}, red[] = {1, 0, 0, 1};
	const float green[] = {0, 1, 0, 1}, white[] = {1, 1, 1, 1};
	struct wlr_scene_tree *backdrop = wlr_scene_tree_create(&scene->tree);
	wlr_scene_rect_create(backdrop, 32, 32, backdrop_color);
	struct wlr_scene_rect *stripe = wlr_scene_rect_create(backdrop, 4, 32, red);
	wlr_scene_node_set_position(&stripe->node, 8, 0);
	struct wlr_scene_tree *workspace = wlr_scene_tree_create(&scene->tree);
	const struct wlr_box viewport = {0, 0, 16, 16};
	wlr_scene_tree_set_clip(workspace, &viewport);
	struct wlr_scene_tree *active = wlr_scene_tree_create(workspace);
	wlr_scene_rect_create(active, 16, 16, red);
	struct wlr_scene_tree *hidden = wlr_scene_tree_create(workspace);
	struct wlr_scene_blur *blur = wlr_scene_blur_create(hidden, 8, 8);
	wlr_scene_node_set_position(&blur->node, 12, 8);
	struct wlr_scene_tree *client_clip = wlr_scene_tree_create(hidden);
	wlr_scene_node_set_position(&client_clip->node, 3, 3);
	wlr_scene_tree_set_clip(client_clip, &(struct wlr_box){1, 1, 4, 4});
	struct wlr_buffer *client_pixels = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 6, 6);
	struct wlr_render_pass *pass = client_pixels ? wlr_renderer_begin_buffer_pass(fixture->renderer, client_pixels, NULL) : NULL;
	if (!pass) return false;
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, 6, 6}, .color = {0, 0, 1, 1}});
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, 3, 6}, .color = {1, 0, 0, 1}});
	bool ok = wlr_render_pass_submit(pass);
	struct wlr_scene_buffer *client = wlr_scene_buffer_create(client_clip, client_pixels);
	wlr_buffer_drop(client_pixels);
	struct samples samples = {.listener.notify = sampled};
	wl_signal_add(&client->events.output_sample, &samples.listener);
	struct wlr_scene_rect *off_viewport = wlr_scene_rect_create(hidden, 6, 6, green);
	wlr_scene_node_set_position(&off_viewport->node, 22, 3);
	struct wlr_scene_border *border = wlr_scene_border_create(hidden, white, white);
	wlr_scene_border_set_geometry(border, 8, 8, 1, 0,
		(struct clipped_region){.area = {1, 1, 6, 6}}, (struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 2, 2);
	struct wlr_scene_tree *lights = wlr_scene_tree_create(&scene->tree);
	wlr_scene_set_effect_light_layer(scene, lights);
	struct wlr_scene_rect *pinned = wlr_scene_rect_create(&scene->tree, 2, 2, green);
	wlr_scene_node_set_position(&pinned->node, 14, 0);
	struct wlr_scene_rect *overlay = wlr_scene_rect_create(&scene->tree, 100000, 16, white);
	struct fx_effect_shader *light_shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv) { return vec4(0.0, 0.0, 0.8, 1.0); }", "virtual-light");
	struct fx_effect_shader *window_shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { vec4 c=umbriel_sample(uv); return vec4(c.a-c.rgb,c.a); }", "virtual-window");
	struct fx_effect_shader *group_shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { return umbriel_sample(vec2(1.0-uv.x,uv.y)); }", "virtual-group");
	struct fx_animation_parameters parameters = {.progress = 1, .linear_progress = 1, .direction = 1};
	struct fx_animation_parameters lighting = parameters;
	lighting.light = (struct fx_effect_light){.enabled = true, .spread = 2, .intensity = 2, .threshold = 0.1f};
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, light_shader, &lighting);
	wlr_scene_node_set_animation(&client_clip->node, FX_SLOT_WINDOWS_MOVE, group_shader, &parameters);
	// A huge excluded shader must not influence the exact desktop-range budget.
	wlr_scene_node_set_animation(&overlay->node, FX_SLOT_WINDOW, window_shader, &parameters);
	struct wlr_buffer *source = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	ok &= check(source && light_shader && window_shader && group_shader, "virtual source resources");
	for (unsigned framing = 0; ok && framing < 3; framing++) {
		for (unsigned role = 0; ok && role < 2; role++) {
			// Native oracle scales the complete output. Enlarge only shared
			// geometry to keep its final face-coordinate size/position fixed.
			wlr_scene_rect_set_size(pinned, framing == 2 ? 4 : 2, framing == 2 ? 4 : 2);
			wlr_scene_node_set_position(&pinned->node, framing == 2 ? 28 : 14, 0);
			wlr_scene_rect_set_size(stripe, framing == 2 ? 8 : 4, 32);
			wlr_scene_node_set_position(&stripe->node, framing == 2 ? 16 : 8, 0);
			wlr_scene_node_set_enabled(&active->node, false);
			wlr_scene_node_set_enabled(&hidden->node, true);
			wlr_scene_node_set_position(&hidden->node, 0, 0);
			wlr_scene_node_set_enabled(&overlay->node, false);
			wlr_scene_tree_set_clip(workspace, framing ? NULL : &viewport);
			wlr_scene_node_set_animation(&hidden->node, FX_SLOT_WINDOW, role ? NULL : window_shader, &parameters);
			struct wlr_output_state state;
			wlr_output_state_init(&state);
			wlr_output_state_set_scale(&state, framing ? 0.5f : 1);
			ok &= wlr_output_commit_state(fixture->output, &state);
			wlr_output_state_finish(&state);
			struct wlr_buffer *native = fixture_render_scene(fixture, output, &state);
			ok &= check(native != NULL, "virtual source native oracle");
			wlr_output_state_finish(&state);
			wlr_output_state_init(&state);
			wlr_output_state_set_scale(&state, 1);
			ok &= wlr_output_commit_state(fixture->output, &state);
			wlr_output_state_finish(&state);
			wlr_scene_tree_set_clip(workspace, &viewport);
			wlr_scene_rect_set_size(pinned, 2, 2);
			wlr_scene_node_set_position(&pinned->node, 14, 0);
			wlr_scene_rect_set_size(stripe, 4, 32);
			wlr_scene_node_set_position(&stripe->node, 8, 0);
			wlr_scene_node_set_animation(&hidden->node, FX_SLOT_WINDOW, window_shader, &parameters);
			wlr_scene_node_set_enabled(&hidden->node, false);
			wlr_scene_node_set_position(&hidden->node, 100, 0);
			wlr_scene_node_set_enabled(&active->node, true);
			wlr_scene_node_set_enabled(&overlay->node, true);
			ok &= check(wl_list_empty(&lights->children), "hidden owner has no native emission proxy");
			struct fx_scene_source_root_override roots[] = {
				{.root = &active->node, .visibility = FX_SCENE_SOURCE_HIDDEN},
				{.root = &hidden->node, .visibility = FX_SCENE_SOURCE_VISIBLE, .offset_x = -100},
				{.root = &backdrop->node, .framing = framing == 2 ? FX_SCENE_SOURCE_VIEWPORT : FX_SCENE_SOURCE_CONTENT},
				{.root = &pinned->node, .framing = framing == 2 ? FX_SCENE_SOURCE_VIEWPORT : FX_SCENE_SOURCE_CONTENT},
				{.root = &lights->node, .framing = framing == 2 ? FX_SCENE_SOURCE_VIEWPORT : FX_SCENE_SOURCE_CONTENT},
			};
			struct wlr_scene_tree *bypass[] = {workspace};
			struct fx_scene_source_view view = {.first = &backdrop->node, .last = &pinned->node,
				.roots = roots, .root_count = 5, .bypass_clips = bypass, .bypass_clip_count = framing ? 1 : 0,
				.extent = {-8, 16, framing ? 32 : 16, framing ? 32 : 16}, .scale = framing ? 0.5f : 1, .viewport = {-8, 16, 16, 16}};
			uint64_t bytes = fx_scene_source_view_bytes_for_test(output, &view);
			struct fx_scene_source_view_plan plan;
			ok &= check(fx_scene_source_view_plan_for_test(output, &view, &plan)
				&& plan.total_bytes == bytes && plan.total_bytes == plan.retained_bytes + plan.capture_bytes
				&& plan.width == 16 && plan.height == 16, "source plan separates retained images from reusable capture peak");
			float content_transform[4], shared_transform[4];
			ok &= check(fx_scene_source_view_framing_for_test(&view, FX_SCENE_SOURCE_CONTENT, content_transform)
				&& fx_scene_source_view_framing_for_test(&view, FX_SCENE_SOURCE_VIEWPORT, shared_transform)
				&& content_transform[0] == (framing ? 0.5f : 1)
				&& content_transform[2] == (framing ? 4 : 8)
				&& content_transform[3] == (framing ? -8 : -16)
				&& shared_transform[0] == 1 && shared_transform[2] == 8 && shared_transform[3] == -16,
				"exact content and shared framing metadata matches rendered band transforms");
			struct wlr_box visual;
			ok &= check(!fx_scene_source_working_space(output) &&
				fx_scene_source_view_bounds_for_test(output, &view, FX_SCENE_SOURCE_CONTENT, &visual),
				"source visual bounds query validates selected virtual geometry");
			if (framing == 2) {
				ok &= check(visual.x <= -20 && visual.y <= 4 && visual.x + visual.width >= 20
					&& visual.y + visual.height >= 40,
					"fit content bounds include cold light halo and off-viewport transient content");
			}
			unsigned before_samples = samples.count;
			pixman_region32_t before_damage;
			pixman_region32_init(&before_damage);
			pixman_region32_copy(&before_damage, &output->pending_commit_damage);
			ok &= check(bytes > 0 && !fx_scene_capture_view_for_test(output, &view, source, role, bytes - 1),
				"virtual source requires complete reservation");
			ok &= check(bytes > 0 && fx_scene_capture_view_for_test(output, &view, source, role, bytes),
				"capture hidden workspace with source-only transforms and cold emission");
			ok &= native && equal(fixture, native, source);
			struct fx_scene_source_pair_for_test pair = {0};
			ok &= check(!fx_scene_source_view_pair_capture_for_test(output, &view, bytes - 1, &pair)
				&& pair.display == NULL && pair.unfiltered == NULL,
				"insufficient owned-view reservation publishes no images");
			ok &= check(fx_scene_source_view_pair_capture_for_test(output, &view, bytes, &pair)
				&& pair.display != pair.unfiltered && pair.reserved_bytes == bytes && !pair.working_space,
				"owned view publishes independent filtered and unfiltered roles atomically");
			ok &= native && equal(fixture, native, role ? pair.unfiltered : pair.display);
			fx_scene_source_pair_finish_for_test(&pair);

			ok &= check(!hidden->node.enabled && hidden->node.x == 100 && active->node.enabled
				&& wl_list_empty(&lights->children) && samples.count == before_samples
				&& pixman_region32_equal(&before_damage, &output->pending_commit_damage),
				"source view preserves native visibility, geometry, emission tree, callbacks and damage");
			pixman_region32_fini(&before_damage);
			roots[1].root = roots[0].root;
			ok &= check(fx_scene_source_view_bytes_for_test(output, &view) == 0, "duplicate override rejects complete view");
			if (native) wlr_buffer_unlock(native);
		}
	}
	wlr_scene_node_clear_animations(&hidden->node);
	wlr_scene_node_clear_animations(&client_clip->node);
	wlr_scene_node_clear_animations(&border->node);
	struct fx_scene_source_root_override plain_roots[] = {
		{.root = &active->node, .visibility = FX_SCENE_SOURCE_HIDDEN},
		{.root = &hidden->node, .visibility = FX_SCENE_SOURCE_VISIBLE, .offset_x = -100},
	};
	struct fx_scene_source_view plain = {.first = &backdrop->node, .last = &pinned->node,
		.roots = plain_roots, .root_count = 2, .extent = {-8, 16, 16, 16}, .scale = 1};
	uint64_t plain_bytes = fx_scene_source_view_bytes_for_test(output, &plain);
	struct fx_scene_source_pair_for_test alias = {0};
	ok &= check(plain_bytes && fx_scene_source_view_pair_capture_for_test(output, &plain, plain_bytes, &alias)
		&& alias.display == alias.unfiltered, "equal source roles alias despite excluded overlay shader");
	fx_scene_source_pair_finish_for_test(&alias);
	// An inventory owns all face images but reuses one sequential capture peak.
	// Downscaling changes both retained images and actual source scratch sizes.
	plain.extent = (struct wlr_box){-8, 16, 1920, 1080};
	plain.scale = 0.25f;
	struct fx_scene_source_view_plan inventory_plan;
	ok &= check(fx_scene_source_view_plan_for_test(output, &plain, &inventory_plan)
		&& inventory_plan.width == (fixture->output->transform & 1 ? 270 : 480) && inventory_plan.height == (fixture->output->transform & 1 ? 480 : 270)
		&& inventory_plan.retained_bytes * 64 + inventory_plan.capture_bytes < FX_SCENE_OUTPUT_BUDGET
		&& inventory_plan.retained_bytes == 480u*270u*4u && inventory_plan.scratch_bytes == 2u*480u*270u*4u,
		"64 aliased faces fit with one shared blur ping-pong peak");

	wl_list_remove(&samples.listener.link);
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(light_shader);
	fx_effect_shader_unref(window_shader);
	fx_effect_shader_unref(group_shader);
	if (source) wlr_buffer_drop(source);
	return ok;
}
static bool target_bypass(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_tree *workspace = wlr_scene_tree_create(&scene->tree);
	struct wlr_scene_tree *target = wlr_scene_tree_create(workspace);
	const float border_color[] = {0.4f, 0.1f, 0.05f, 0.5f}, shadow_color[] = {0, 0, 0, 0.4f}, zero[4] = {0};
	struct wlr_scene_shadow *shadow = wlr_scene_shadow_create(target, 12, 12, 1, 2, shadow_color);
	wlr_scene_node_set_position(&shadow->node, 1, 1);
	struct wlr_buffer *pixels = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 8, 8);
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, pixels, NULL);
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, 8, 8}, .color = {0.3f, 0.2f, 0.1f, 0.5f}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
	bool ok = wlr_render_pass_submit(pass);
	struct wlr_scene_buffer *content = wlr_scene_buffer_create(target, pixels);
	wlr_buffer_drop(pixels);
	wlr_scene_node_set_position(&content->node, 3, 3);
	wlr_scene_buffer_set_opacity(content, 0.6f);
	struct wlr_scene_border *border = wlr_scene_border_create(target, border_color, border_color);
	wlr_scene_border_set_geometry(border, 10, 10, 1, 0, (struct clipped_region){.area = {1, 1, 8, 8}},
		(struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 2, 2);
	// Preserve this unrelated stage while bypassing only the target's lifecycle.
	struct fx_effect_shader *move = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv){vec4 c=umbriel_sample(uv);return vec4(c.b,c.r,c.g,c.a)*0.75;}", "participant-unrelated-move");
	struct fx_effect_shader *opening = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv){return umbriel_sample_previous(uv)*0.5;}", "participant-bypassed-feedback-opening");
	struct fx_animation_parameters params = {.progress = 0.5f, .linear_progress = 0.5f, .direction = 1};
	wlr_scene_node_set_animation(&target->node, FX_SLOT_WINDOWS_MOVE, move, &params);
	struct wlr_buffer *reference = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	ok &= move && opening && fx_scene_capture_participant_extent_for_test(output, &target->node, reference,
		&(struct wlr_box){0, 0, 16, 16});
	wlr_scene_buffer_set_opacity(content, 0);
	wlr_scene_border_set_colors(border, zero, zero);
	wlr_scene_shadow_set_color(shadow, zero);
	wlr_scene_node_set_animation(&target->node, FX_SLOT_WINDOWS_IN, opening, &params);
	wlr_scene_node_set_animation_output_clip(&target->node, &(struct wlr_box){0});
	struct fx_scene_source_node_override overrides[] = {
		{.node = &target->node, .skip_slots = 1u << FX_SLOT_WINDOWS_IN, .skip_animation_clip = true},
		{.node = &content->node, .has_opacity = true, .opacity = 0.6f},
		{.node = &border->node, .has_colors = true, .colors = {{0.4f, 0.1f, 0.05f, 0.5f}, {0.4f, 0.1f, 0.05f, 0.5f}}},
		{.node = &shadow->node, .has_colors = true, .colors = {{0, 0, 0, 0.4f}}},
	};
	struct fx_scene_source_view view = {.first = &target->node, .last = &target->node,
		.extent = {0, 0, 16, 16}, .scale = 1, .transparent = true, .nodes = overrides, .node_count = 4};
	uint64_t bytes = fx_scene_source_view_bytes_for_test(output, &view);
	struct fx_scene_source_pair_for_test pair = {0};
	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_copy(&damage, &output->pending_commit_damage);
	ok &= check(bytes && fx_scene_source_view_pair_capture_for_test(output, &view, bytes, &pair),
		"nested target pair bypasses only replaced lifecycle even with zero native fade");
	ok &= pair.display && equal(fixture, reference, pair.display) && equal(fixture, reference, pair.unfiltered);
	ok &= check(content->opacity == 0 && border->inner_color[3] == 0 && shadow->color[3] == 0 &&
		pixman_region32_equal(&damage, &output->pending_commit_damage), "target bypass preserves native opacity/colors/damage");
	fx_scene_source_pair_finish_for_test(&pair);
	struct fx_effect_shader *cold = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv){return vec4(0.8,0.2,0.1,1.0);}", "participant-cold-emission");
	params.light = (struct fx_effect_light){.enabled = true, .spread = 3, .intensity = 1.5f, .threshold = 0.4f};
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, cold, &params);
	pixman_region32_copy(&damage, &output->pending_commit_damage);
	struct fx_scene_emission_source raw = {0};
	uint64_t raw_bytes = fx_scene_emission_view_bytes(output, &view, &border->node);
	ok &= check(cold && raw_bytes && fx_scene_emission_source_bytes(output, &border->node) == 0,
		"cold border has no committed native emission or light proxy");
	ok &= check(!fx_scene_emission_view_capture(output, &view, &border->node, raw_bytes - 1, &raw) && raw.display == NULL,
		"cold emission reserves complete raw roles and capture peak");
	ok &= check(fx_scene_emission_view_capture(output, &view, &border->node, raw_bytes, &raw)
		&& raw.display && raw.unfiltered && raw.extent.x == 2 && raw.extent.y == 2 && raw.extent.width == 10
		&& raw.extent.height == 10 && raw.recipe.threshold == 0.4f && !raw.working_space,
		"cold source-only emission exports exact owner extent and held recipe");
	for (unsigned role = 0; ok && role < 2; role++) {
		struct wlr_buffer *image = role ? raw.unfiltered : raw.display;
		struct fx_framebuffer *fb = fx_framebuffer_get_or_create(fx_get_renderer(fixture->renderer), image);
		unsigned count = image->width * image->height * 4;
		float red = 0;
		if (fb->drm_format == DRM_FORMAT_ABGR16161616F) {
			uint16_t pixels[count];
			ok &= read_buffer(fixture, image, DRM_FORMAT_ABGR16161616F, image->width * 8, pixels);
			for (unsigned i = 0; i < count; i += 4) {
				int exponent = (pixels[i] >> 10) & 31;
				float value = exponent ? ldexpf(1 + (pixels[i] & 1023) / 1024.0f, exponent - 15) : ldexpf(pixels[i] & 1023, -24);
				red = fmaxf(red, value);
			}
		} else {
			uint8_t pixels[count];
			ok &= read_buffer(fixture, image, DRM_FORMAT_ABGR8888, image->width * 4, pixels);
			for (unsigned i = 0; i < count; i += 4) red = fmaxf(red, pixels[i] / 255.0f);
		}
		ok &= check(fabsf(red - 0.8f) < 0.005f, "cold raw emission precedes threshold and unrelated parent compositing");
	}
	ok &= check(fx_scene_emission_source_bytes(output, &border->node) == 0
		&& pixman_region32_equal(&damage, &output->pending_commit_damage) && border->inner_color[3] == 0,
		"cold acquisition leaves native light ownership/properties/damage untouched");
	fx_scene_emission_source_finish(&raw);
	fx_effect_shader_unref(cold);
	view.node_count = 0;
	struct fx_scene_source_view_plan history_plan;
	ok &= check(fx_scene_source_view_plan_for_test(output, &view, &history_plan) && history_plan.history_bytes > 0,
		"unbypassed feedback lifecycle requires an explicit history reservation");
	pixman_region32_fini(&damage);
	wlr_buffer_drop(reference);
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(move); fx_effect_shader_unref(opening);
	return ok;
}

static bool rectangular(struct fixture *fixture) {
	bool ok = true;
	for (unsigned scale_index = 0; ok && scale_index < 2; scale_index++) {
		float scale = scale_index ? 1.25f : 1;
		for (unsigned transform = 0; ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
			for (unsigned fit = 0; ok && fit < 2; fit++) {
				float source_scale = scale * (fit ? 0.5f : 1);
				struct wlr_output_state state;
				wlr_output_state_init(&state);
				wlr_output_state_set_enabled(&state, true);
				wlr_output_state_set_custom_mode(&state, 40, 30, 60000);
				wlr_output_state_set_transform(&state, transform);
				wlr_output_state_set_scale(&state, source_scale);
				ok &= wlr_output_commit_state(fixture->output, &state);
				wlr_output_state_finish(&state);
				struct wlr_scene *scene = wlr_scene_create();
				struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
				wlr_scene_output_set_position(output, -12, 8);
				wlr_scene_node_set_position(&scene->tree.node, -12, 8);
				struct wlr_scene_tree *desktop = wlr_scene_tree_create(&scene->tree);
				const float back[] = {0.1f, 0.3f, 0.4f, 1}, red[] = {1, 0, 0, 1};
				wlr_scene_rect_create(desktop, 100, 100, back);
				struct wlr_scene_tree *hidden = wlr_scene_tree_create(desktop);
				struct wlr_scene_rect *offscreen = wlr_scene_rect_create(hidden, 7, 9, red);
				wlr_scene_node_set_position(&offscreen->node, 34, 11);
				struct wlr_scene_tree *clipped = wlr_scene_tree_create(hidden);
				wlr_scene_tree_set_clip(clipped, &(struct wlr_box){1, 2, 7, 9});
				wlr_scene_rect_create(clipped, 5, 6, red);
				struct wlr_swapchain *swapchain = wlr_swapchain_create(fixture->allocator, 40, 30,
					get_render_format(fixture, DRM_FORMAT_ARGB8888));
				wlr_output_state_init(&state);
				ok &= check(swapchain && wlr_scene_output_build_state(output, &state,
					&(struct wlr_scene_output_state_options){.swapchain = swapchain}) && state.buffer,
					"rectangular fractional native reference");
				struct wlr_buffer *native = state.buffer ? wlr_buffer_lock(state.buffer) : NULL;
				wlr_output_state_finish(&state);
				wlr_output_state_init(&state);
				wlr_output_state_set_scale(&state, scale);
				ok &= wlr_output_commit_state(fixture->output, &state);
				wlr_output_state_finish(&state);
				wlr_scene_node_set_enabled(&hidden->node, false);
				wlr_scene_node_set_position(&hidden->node, 100, -50);
				int logical_width = transform & 1 ? 30 : 40, logical_height = transform & 1 ? 40 : 30;
				struct fx_scene_source_root_override override = {.root = &hidden->node,
					.visibility = FX_SCENE_SOURCE_VISIBLE, .offset_x = -100, .offset_y = 50};
				struct fx_scene_source_view view = {.first = &desktop->node, .last = &desktop->node,
					.roots = &override, .root_count = 1, .scale = source_scale,
					.extent = {-12, 8, lroundf(logical_width / source_scale), lroundf(logical_height / source_scale)},
					.viewport = {-12, 8, lroundf(logical_width / scale), lroundf(logical_height / scale)}};
				struct fx_scene_source_view_plan plan;
				struct fx_scene_source_pair_for_test pair = {0};
				ok &= check(fx_scene_source_view_plan_for_test(output, &view, &plan) && plan.width == 40 && plan.height == 30,
					"physical source plan dimensions follow rectangular transform");
				ok &= check(fx_scene_source_view_pair_capture_for_test(output, &view, plan.total_bytes, &pair),
					"rectangular fractional virtual source pair");
				uint8_t a[40 * 30 * 4], b[sizeof(a)];
				ok &= native && pair.display && read_buffer(fixture, native, DRM_FORMAT_ARGB8888, 40 * 4, a)
					&& read_buffer(fixture, pair.display, DRM_FORMAT_ARGB8888, 40 * 4, b);
				for (size_t i = 0; ok && i < sizeof(a); i++) {
					if (!check(abs(a[i] - b[i]) <= 1, "rectangular virtual view matches native at fractional scale")) {
						fprintf(stderr, "scale=%f transform=%u fit=%u byte=%zu native=%u virtual=%u\n", scale, transform, fit, i, a[i], b[i]);
						ok = false;
					}
				}
				ok &= check(!hidden->node.enabled && hidden->node.x == 100 && hidden->node.y == -50,
					"rectangular source leaves native geometry untouched");
				fx_scene_source_pair_finish_for_test(&pair);
				if (native) wlr_buffer_unlock(native);
				if (swapchain) wlr_swapchain_destroy(swapchain);
				wlr_scene_node_destroy(&scene->tree.node);
			}
		}
	}
	return ok;
}

static bool budget_matrix(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float white[] = {1, 1, 1, 1};
	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 3840, 2160, white);
	struct wlr_scene_optimized_blur *blur = wlr_scene_optimized_blur_create(&scene->tree, 3840, 2160);
	scene->blur_data = (struct blur_data){.radius = 4, .num_passes = 2, .brightness = 1, .contrast = 1, .saturation = 1};
	struct wlr_color_transform *linear = wlr_color_transform_init_linear_to_inverse_eotf(WLR_COLOR_TRANSFER_FUNCTION_SRGB);
	bool ok = linear != NULL;
	for (unsigned precision = 0; ok && precision < 2; precision++) {
		output->combined_color_transform = precision ? wlr_color_transform_ref(linear) : NULL;
		for (unsigned size = 0; size < 2; size++) {
			unsigned width = size ? 3840 : 1920, height = size ? 2160 : 1080;
			for (unsigned optimized = 0; optimized < 2; optimized++) {
				wlr_scene_node_set_enabled(&blur->node, optimized);
				struct fx_scene_source_view view = {.first = &rect->node, .last = &blur->node,
					.extent = {0, 0, width, height}, .scale = 1};
				struct fx_scene_source_view_plan plan;
				bool admitted = fx_scene_source_view_plan_for_test(output, &view, &plan);
				fprintf(stderr, "source budget %ux%u %s optimized=%u admitted=%d retained=%llu capture=%llu total=%llu\n",
					width, height, precision ? "FP16" : "RGBA8", optimized, admitted,
					(unsigned long long)plan.retained_bytes, (unsigned long long)plan.capture_bytes, (unsigned long long)plan.total_bytes);
				bool expected = !(precision && size && optimized);
				ok &= check(admitted == expected, "feature-sensitive source admission matrix");
				if (admitted) {
					uint64_t image = (uint64_t)width*height*(precision ? 8 : 4);
					ok &= check(plan.retained_bytes == image && plan.scratch_bytes == image*(optimized ? 4 : 0)
						&& plan.capture_bytes > plan.scratch_bytes,
						"alias proof charges one retained role and exact plain/blur scratch classes");
				}
			}
		}
		if (output->combined_color_transform) wlr_color_transform_unref(output->combined_color_transform);
		output->combined_color_transform = NULL;
	}
	wlr_color_transform_unref(linear);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

static bool virtual_history(struct fixture *fixture, bool bands) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
	const float blue[] = {0, 0, 1, 1};
	struct wlr_scene_rect *rect = wlr_scene_rect_create(tree, 16, 16, blue);
	struct wlr_scene_rect *panel = wlr_scene_rect_create(&scene->tree, 1, 1, blue);
	struct fx_effect_shader *accumulate = fx_effect_shader_create(fixture->renderer, FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv) { vec4 p=umbriel_sample_previous(uv); return vec4(p.r+0.25,p.b,umbriel_sample(uv).b,1.0); }", "virtual-feedback");
	struct fx_effect_shader *green = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return vec4(0.0,1.0,0.0,1.0); }", "virtual-feedback-green");
	struct fx_animation_parameters parameters = {.progress=1, .linear_progress=1, .direction=1};
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOW, green, &parameters);
	wlr_scene_node_set_animation(&rect->node, FX_SLOT_WINDOWS_IN, accumulate, &parameters);
	wlr_scene_output_set_effect_capture_policy(output, false);
	wlr_scene_node_set_enabled(&tree->node,false);
	struct fx_scene_source_root_override roots[] = {
		{.root=&tree->node, .visibility=FX_SCENE_SOURCE_VISIBLE},
		{.root=&panel->node, .framing=bands ? FX_SCENE_SOURCE_VIEWPORT : FX_SCENE_SOURCE_CONTENT},
	};
	struct fx_scene_source_view view = {.first=&tree->node, .last=&panel->node, .roots=roots, .root_count=2,
		.extent={0,0,16,16}, .viewport={0,0,16,16}, .scale=1};
	struct fx_scene_source_view_plan plan;
	bool ok = check(accumulate && green && fx_scene_source_view_plan_for_test(output,&view,&plan)
		&& plan.history_bytes > 0 && plan.total_bytes == plan.retained_bytes+plan.capture_bytes,
		"virtual feedback plans histories separately from reusable capture scratch");
	struct fx_scene_source_pair_for_test pair = {0};
	ok &= check(!fx_scene_source_view_pair_capture_for_test(output,&view,plan.total_bytes,&pair),
		"feedback view requires a separately reserved open occurrence session");
	ok &= check(!fx_scene_source_view_session_create(output,&view,plan.history_bytes-1), "history reserve enforced");
	struct fx_scene_source_session *a=fx_scene_source_view_session_create(output,&view,plan.history_bytes);
	struct fx_scene_source_session *b=fx_scene_source_view_session_create(output,&view,plan.history_bytes);
	ok &= check(a && b, "two independent virtual occurrences admitted");
	for (unsigned frame=0; ok && frame<3; frame++) {
		view.session=a;
		ok &= check(fx_scene_source_view_session_matches(output,&view,a)
			&& fx_scene_source_session_begin_frame_for_test(a), "begin matching occurrence frame");
		ok &= check(fx_scene_source_view_pair_capture_for_test(output,&view,plan.total_bytes,&pair), "capture paired virtual histories");
		uint8_t shown[4], plain[4];
		ok &= fixture_read_pixel(fixture,pair.display,8,8,shown) && fixture_read_pixel(fixture,pair.unfiltered,8,8,plain);
		int red=frame<2 ? 64 : 128;
		fprintf(stderr,"virtual feedback bands%u frame%u display=%u,%u,%u capture=%u,%u,%u\n",bands,frame,
			shown[2],shown[1],shown[0],plain[2],plain[1],plain[0]);
		ok &= check(abs(shown[2]-red)<=1 && abs(plain[2]-red)<=1 && shown[0]<2 && shown[1]<2
			&& plain[0]>253 && plain[1]>253,
			"failed frame preserves both role histories; success promotes independently");
		fx_scene_source_session_finish_frame_for_test(a,frame!=0);
		fx_scene_source_pair_finish_for_test(&pair);
		// Ordinary native rendering must not become this occurrence's feedback.
		struct wlr_output_state state;
		wlr_scene_node_set_enabled(&tree->node,true);
		struct wlr_buffer *native=fixture_render_scene(fixture,output,&state);
		ok &= check(native!=NULL,"interleave ordinary native feedback");
		wlr_output_state_finish(&state);
		if(native) wlr_buffer_unlock(native);
		wlr_scene_node_set_enabled(&tree->node,false);
	}
	view.session=b;
	ok &= check(fx_scene_source_session_begin_frame_for_test(b)
		&& fx_scene_source_view_pair_capture_for_test(output,&view,plan.total_bytes,&pair), "second occurrence starts empty");
	if(pair.display) {
		uint8_t pixel[4]; ok &= fixture_read_pixel(fixture,pair.display,8,8,pixel);
		ok &= check(abs(pixel[2]-64)<=1,"other face never borrows promoted native or first face history");
	}
	fx_scene_source_session_finish_frame_for_test(b,false);
	fx_scene_source_pair_finish_for_test(&pair);
	view.extent.width=32;
	ok &= check(!fx_scene_source_view_session_matches(output,&view,a),"framing change requests fresh session");
	view.extent.width=16;
	wlr_scene_rect_set_size(rect,32,16);
	ok &= check(!fx_scene_source_view_session_matches(output,&view,a),"geometry beyond reservation requests fresh session");
	wlr_scene_rect_set_size(rect,16,16);
	wlr_scene_node_clear_animations(&rect->node);
	ok &= check(!fx_scene_source_view_session_matches(output,&view,a),"changed program inventory requests fresh session");
	wlr_scene_node_destroy(&scene->tree.node);
	ok &= check(!fx_scene_source_session_begin_frame_for_test(a),"destroyed virtual owner invalidates session");
	fx_scene_source_session_destroy_for_test(a);
	fx_scene_source_session_destroy_for_test(b);
	fx_effect_shader_unref(accumulate);
	fx_effect_shader_unref(green);
	return ok;
}

static bool shader_scratch_matrix(struct fixture *fixture) {
	struct wlr_scene *scene=wlr_scene_create();
	struct wlr_scene_output *output=wlr_scene_output_create(scene,fixture->output);
	struct wlr_scene_tree *group=wlr_scene_tree_create(&scene->tree);
	const float blue[]={0,0,1,1};
	struct wlr_scene_rect *rect=wlr_scene_rect_create(group,16,16,blue);
	struct wlr_scene_blur *blur=wlr_scene_blur_create(group,4,4);
	struct wlr_scene_optimized_blur *optimized=wlr_scene_optimized_blur_create(group,16,16);
	struct fx_effect_shader *shader=fx_effect_shader_create(fixture->renderer,FX_EFFECT_ANIMATION,
		"vec4 animation(vec2 uv){return umbriel_sample(uv);}","scratch-stage");
	struct fx_effect_shader *inplace=fx_effect_shader_create(fixture->renderer,FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv){return umbriel_sample(uv);}","scratch-inplace");
	struct fx_animation_parameters parameters={.progress=1,.linear_progress=1,.direction=1};
	struct fx_scene_source_view view={.first=&group->node,.last=&group->node,.extent={0,0,16,16},.scale=1};
	bool ok=shader && inplace;
	const unsigned images[]={0,1,2,4,12,14};
	for(unsigned kind=0;ok && kind<6;kind++) {
		wlr_scene_node_set_enabled(&blur->node,kind>=4);
		wlr_scene_node_set_enabled(&optimized->node,kind==5);
		wlr_scene_node_set_animation(&rect->node,FX_SLOT_WINDOW,kind==1 || kind>=4 ? inplace:NULL,&parameters);
		wlr_scene_node_set_animation(&rect->node,FX_SLOT_WINDOWS_IN,kind>=2 ? shader:NULL,&parameters);
		wlr_scene_node_set_animation(&group->node,FX_SLOT_WINDOWS_MOVE,kind>=3 ? shader:NULL,&parameters);
		struct fx_scene_source_view_plan plan;
		ok &= check(fx_scene_source_view_plan_for_test(output,&view,&plan),"shader scratch class admits");
		fprintf(stderr,"source scratch class%u bytes=%llu images=%u\n",kind,(unsigned long long)plan.scratch_bytes,images[kind]);
		ok &= check(plan.scratch_bytes==(uint64_t)16*16*4*images[kind],
			"scratch preflight counts only selected independent pool slots");
		struct fx_scene_source_pair_for_test pair={0};
		ok &= check(!fx_scene_source_view_pair_capture_for_test(output,&view,plan.total_bytes-1,&pair)
			&& !pair.display && !pair.unfiltered,"shader scratch cannot exceed reservation");
		ok &= check(fx_scene_source_view_pair_capture_for_test(output,&view,plan.total_bytes,&pair),
			"admitted nested/in-place/backdrop/blur scratch renders both roles");
		fx_scene_source_pair_finish_for_test(&pair);
	}
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(shader); fx_effect_shader_unref(inplace);
	return ok;
}


struct source_hit_snapshot {
	struct wlr_scene_node *nodes[16];
	struct wlr_fbox boxes[16];
	size_t count;
};
static bool snapshot_hit(struct wlr_scene_node *node, const struct wlr_fbox *box, void *data) {
	struct source_hit_snapshot *snapshot = data;
	if (snapshot->count >= 16) return false;
	snapshot->nodes[snapshot->count] = node;
	snapshot->boxes[snapshot->count++] = *box;
	return true;
}
static bool hit_box(const struct source_hit_snapshot *snapshot, size_t index,
		struct wlr_scene_node *node, double x, double y, double width, double height) {
	if (index >= snapshot->count) return check(false, "source hit snapshot has expected leaf");
	const struct wlr_fbox *box = &snapshot->boxes[index];
	return check(snapshot->nodes[index] == node && fabs(box->x - x) < 0.0001
		&& fabs(box->y - y) < 0.0001 && fabs(box->width - width) < 0.0001
		&& fabs(box->height - height) < 0.0001, "source hit snapshot order and exact face-local geometry");
}
static bool hit_snapshots(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	wlr_scene_output_set_position(output, -8, 16);
	wlr_scene_node_set_position(&scene->tree.node, -8, 16);
	const float white[] = {1, 1, 1, 1}, clear[] = {0, 0, 0, 0};
	struct wlr_scene_tree *workspace = wlr_scene_tree_create(&scene->tree);
	const struct wlr_box viewport_clip = {0, 0, 16, 16};
	wlr_scene_tree_set_clip(workspace, &viewport_clip);
	struct wlr_scene_tree *active = wlr_scene_tree_create(workspace);
	wlr_scene_rect_create(active, 16, 16, white);
	struct wlr_scene_tree *hidden = wlr_scene_tree_create(workspace);
	wlr_scene_node_set_position(&hidden->node, 100, 0);
	wlr_scene_node_set_enabled(&hidden->node, false);
	struct wlr_scene_tree *clip = wlr_scene_tree_create(hidden);
	wlr_scene_node_set_position(&clip->node, 3, 3);
	wlr_scene_tree_set_clip(clip, &(struct wlr_box){1, 1, 4, 4});
	struct wlr_scene_rect *first = wlr_scene_rect_create(clip, 6, 6, white);
	struct wlr_scene_rect *second = wlr_scene_rect_create(hidden, 2, 2, white);
	wlr_scene_node_set_position(&second->node, 5, 4);
	struct wlr_scene_rect *outside = wlr_scene_rect_create(hidden, 6, 6, white);
	wlr_scene_node_set_position(&outside->node, 22, 3);
	struct wlr_scene_rect *transparent = wlr_scene_rect_create(hidden, 3, 3, clear);
	struct wlr_buffer *pixels = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 3, 3);
	struct wlr_scene_buffer *zero = wlr_scene_buffer_create(hidden, pixels);
	if (pixels) wlr_buffer_drop(pixels);
	wlr_scene_buffer_set_opacity(zero, 0);
	wlr_scene_shadow_create(hidden, 12, 12, 2, 1, white);
	struct wlr_scene_rect *pinned = wlr_scene_rect_create(&scene->tree, 2, 2, white);
	wlr_scene_node_set_position(&pinned->node, 14, 0);
	struct fx_scene_source_root_override roots[] = {
		{.root = &active->node, .visibility = FX_SCENE_SOURCE_HIDDEN},
		{.root = &hidden->node, .visibility = FX_SCENE_SOURCE_VISIBLE, .offset_x = -100},
		{.root = &pinned->node, .framing = FX_SCENE_SOURCE_VIEWPORT},
	};
	struct wlr_scene_tree *bypass[] = {workspace};
	struct fx_scene_source_view view = {.first = &workspace->node, .last = &pinned->node,
		.roots = roots, .root_count = 3, .bypass_clips = bypass, .bypass_clip_count = 1,
		.extent = {-8, 16, 32, 32}, .scale = 0.5f, .viewport = {-8, 16, 16, 16}};
	pixman_region32_t before_damage;
	pixman_region32_init(&before_damage);
	pixman_region32_copy(&before_damage, &output->pending_commit_damage);
	struct source_hit_snapshot snapshot = {0};
	size_t count = 0;
	bool ok = check(pixels != NULL && fx_scene_source_view_hits(output, &view, NULL, NULL, &count)
		&& count == 4, "source hit reservation excludes hidden, transparent and shadow leaves");
	ok &= check(fx_scene_source_view_hits(output, &view, snapshot_hit, &snapshot, &count)
		&& count == snapshot.count && count == 4, "source hit capture matches reservation count");
	ok &= hit_box(&snapshot, 0, &first->node, 2, 2, 2, 2);
	ok &= hit_box(&snapshot, 1, &second->node, 2.5, 2, 1, 1);
	ok &= hit_box(&snapshot, 2, &outside->node, 11, 1.5, 3, 3);
	ok &= hit_box(&snapshot, 3, &pinned->node, 14, 0, 2, 2);
	ok &= check(!hidden->node.enabled && hidden->node.x == 100 && active->node.enabled
		&& pixman_region32_equal(&before_damage, &output->pending_commit_damage),
		"hit snapshot does not mutate native visibility, geometry or damage");
	view.bypass_clip_count = 0;
	struct source_hit_snapshot clipped = {0};
	ok &= check(fx_scene_source_view_hits(output, &view, snapshot_hit, &clipped, &count)
		&& count == 3, "native viewport clip survives snapshot and removes off-viewport hit unless explicitly bypassed");
	ok &= hit_box(&clipped, 2, &pinned->node, 14, 0, 2, 2);
	view.bypass_clip_count = 1;
	wlr_scene_node_place_below(&second->node, &clip->node);
	struct source_hit_snapshot reordered = {0};
	ok &= check(fx_scene_source_view_hits(output, &view, snapshot_hit, &reordered, &count),
		"reordered source hit snapshot succeeds");
	ok &= hit_box(&reordered, 0, &second->node, 2.5, 2, 1, 1);
	ok &= hit_box(&reordered, 1, &first->node, 2, 2, 2, 2);
	ok &= hit_box(&snapshot, 0, &first->node, 2, 2, 2, 2);
	struct fx_scene_source_node_override nodes[] = {
		{.node = &first->node, .has_colors = true, .colors = {{0,0,0,0}, {0,0,0,0}}},
		{.node = &transparent->node, .has_colors = true, .colors = {{1,1,1,1}, {1,1,1,1}}},
		{.node = &zero->node, .has_opacity = true, .opacity = 0.5f},
	};
	view.nodes = nodes; view.node_count = 3;
	struct source_hit_snapshot overrides = {0};
	ok &= check(fx_scene_source_view_hits(output, &view, snapshot_hit, &overrides, &count)
		&& count == 5, "hit eligibility honors effective source colors and opacity");
	ok &= hit_box(&overrides, 0, &second->node, 2.5, 2, 1, 1);
	ok &= hit_box(&overrides, 1, &outside->node, 11, 1.5, 3, 3);
	ok &= hit_box(&overrides, 2, &transparent->node, 0, 0, 1.5, 1.5);
	ok &= hit_box(&overrides, 3, &zero->node, 0, 0, 1.5, 1.5);
	ok &= check(first->color[3] == 1 && transparent->color[3] == 0 && zero->opacity == 0,
		"source hit overrides leave native opacity and colors unchanged");
	struct source_hit_snapshot full = {.count = 16};
	ok &= check(!fx_scene_source_view_hits(output, &view, snapshot_hit, &full, &count),
		"snapshot callback capacity failure aborts rather than publishing truncated success");
	pixman_region32_fini(&before_damage);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) { fixture_finish(&fixture); return 77; }
	bool ok = hit_snapshots(&fixture);
	ok &= target_bypass(&fixture);
	ok &= shader_scratch_matrix(&fixture);
	ok &= virtual_history(&fixture, false);
	ok &= virtual_history(&fixture, true);
	for (unsigned transform = 0; ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_state_set_transform(&state, transform);
		ok &= wlr_output_commit_state(fixture.output, &state);
		wlr_output_state_finish(&state);
		fprintf(stderr, "virtual workspace output_transform=%u\n", transform);
		ok &= experiment(&fixture);
	}
	ok &= rectangular(&fixture);
	ok &= budget_matrix(&fixture);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
