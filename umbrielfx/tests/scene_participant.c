// C0/G2 rigid participant experiment. Content, shadows and screen-blended
// emission remain independent ordered sources. This does not admit grid folds.
#include "render_fixture.h"
#include "types/wlr_scene.h"
#include "umbrielfx/render/effect.h"

static bool compare(struct fixture *fixture, struct wlr_buffer *native, struct wlr_buffer *composed) {
	uint8_t a[TEST_WIDTH * TEST_HEIGHT * 4], b[sizeof(a)];
	if (!read_buffer(fixture, native, DRM_FORMAT_ARGB8888, TEST_WIDTH * 4, a) ||
			!read_buffer(fixture, composed, DRM_FORMAT_ARGB8888, TEST_WIDTH * 4, b)) {
		return false;
	}
	for (size_t i = 0; i < sizeof(a); i++) {
		if (abs(a[i] - b[i]) > 2) {
			fprintf(stderr, "participant byte %zu: native=%u composed=%u\n", i, a[i], b[i]);
			return false;
		}
	}
	return true;
}

static bool experiment(struct fixture *fixture, bool with_light) {
	fprintf(stderr, "participant with_light=%d\n", with_light);
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *output = wlr_scene_output_create(scene, fixture->output);
	const float blue[] = {0.1f, 0.2f, 0.5f, 1}, brown[] = {0.4f, 0.2f, 0.1f, 1};
	const float red[] = {0.5f, 0, 0, 0.5f}, green[] = {0, 0.5f, 0, 0.5f};
	const float shadow_color[] = {0, 0, 0, 0.5f}, white[] = {1, 1, 1, 1};
	wlr_scene_rect_create(&scene->tree, 16, 16, blue);
	struct wlr_scene_rect *back = wlr_scene_rect_create(&scene->tree, 8, 16, brown);
	wlr_scene_node_set_position(&back->node, 8, 0);
	struct wlr_scene_shadow *shadow_a = wlr_scene_shadow_create(&scene->tree, 8, 8, 1, 1, shadow_color);
	struct wlr_scene_shadow *shadow_b = wlr_scene_shadow_create(&scene->tree, 8, 8, 1, 1, shadow_color);
	wlr_scene_node_set_position(&shadow_a->node, 2, 3);
	wlr_scene_node_set_position(&shadow_b->node, 6, 4);
	struct wlr_scene_rect *a = wlr_scene_rect_create(&scene->tree, 6, 6, red);
	struct wlr_scene_rect *b = wlr_scene_rect_create(&scene->tree, 6, 6, green);
	wlr_scene_node_set_position(&a->node, 3, 4);
	wlr_scene_node_set_position(&b->node, 7, 5);
	struct wlr_scene_border *border = wlr_scene_border_create(&scene->tree, white, white);
	wlr_scene_border_set_geometry(border, 8, 8, 1, 0,
		(struct clipped_region){.area = {1, 1, 6, 6}},
		(struct fx_corner_radii){0}, (struct fx_corner_radii){0});
	wlr_scene_node_set_position(&border->node, 2, 3);
	struct wlr_scene_tree *lights = wlr_scene_tree_create(&scene->tree);
	wlr_scene_set_effect_light_layer(scene, lights);
	struct fx_effect_shader *shader = fx_effect_shader_create(fixture->renderer, FX_EFFECT_BORDER,
		"vec4 border(vec2 uv) { return vec4(0.0, 0.0, 0.8, 1.0); }", "participant-border");
	struct fx_animation_parameters parameters = {
		.progress = 1, .linear_progress = 1, .direction = 1,
		.light = {.enabled = true, .spread = 2, .intensity = 2, .threshold = 0.1f},
	};
	parameters.light.enabled = with_light;
	wlr_scene_node_set_animation(&border->node, FX_SLOT_BORDER_EFFECT, shader, &parameters);
	struct wlr_scene_node *items[] = {&shadow_a->node, &shadow_b->node, &a->node, &b->node, &border->node, &lights->node};
	const int delta[][2] = {{1, -1}, {-2, 1}, {1, -1}, {-2, 1}, {1, -1}, {1, -1}};
	struct wlr_buffer *sources[6] = {0};
	const struct wlr_box source_extent = {-8, -8, 32, 32};
	bool ok = check(shader != NULL, "participant border shader");
	// Companion acquisition starts from an actually presented native source.
	// Capturing the border must not populate or mutate its native light cache.
	struct wlr_output_state initial_state;
	struct wlr_buffer *initial = fixture_render_scene(fixture, output, &initial_state);
	ok &= check(initial != NULL, "native participant presentation before acquisition");
	if (initial) wlr_buffer_unlock(initial);
	wlr_output_state_finish(&initial_state);

	for (unsigned i = 0; ok && i < 6; i++) {
		sources[i] = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 32, 32);
		ok &= check(sources[i] && fx_scene_capture_participant_extent_for_test(output, items[i], sources[i], &source_extent),
			"independent transparent participant/companion capture");
	}
	struct wlr_buffer *composed = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 16, 16);
	struct wlr_render_pass *pass = composed ? wlr_renderer_begin_buffer_pass(fixture->renderer, composed, NULL) : NULL;
	if (ok && pass) {
		wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
			.box = {.width = 16, .height = 16}, .color = {0.1f, 0.2f, 0.5f, 1}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
		});
		wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
			.box = {8, 0, 8, 16}, .color = {0.4f, 0.2f, 0.1f, 1}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
		});
		for (unsigned i = 0; i < 6; i++) {
			struct wlr_texture *texture = wlr_texture_from_buffer(fixture->renderer, sources[i]);
			ok &= check(texture != NULL, "participant source import");
			if (!texture) {
				continue;
			}
			if (i == 5) {
				glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
			}
			wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
				.texture = texture, .dst_box = {-8 + delta[i][0], -8 + delta[i][1], 32, 32},
				.filter_mode = WLR_SCALE_FILTER_NEAREST, .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
			});
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			wlr_texture_destroy(texture);
		}
	}
	if (pass) {
		ok &= check(wlr_render_pass_submit(pass), "rigid participant composition submits");
	} else {
		ok = false;
	}
	// Native positions are the oracle: two windows move independently, with
	// their own shadow and light companions following the same residual.
	for (unsigned i = 0; i < 5; i++) {
		wlr_scene_node_set_position(items[i], items[i]->x + delta[i][0], items[i]->y + delta[i][1]);
	}
	struct wlr_output_state state;
	struct wlr_buffer *native = fixture_render_scene(fixture, output, &state);
	ok &= native && composed && compare(fixture, native, composed);
	if (native) {
		wlr_buffer_unlock(native);
	}
	wlr_output_state_finish(&state);
	// Admission is selected-presentation based and rechecked after changes.
	struct fx_effect_shader *in_place = fx_effect_shader_create(fixture->renderer, FX_EFFECT_WINDOW,
		"vec4 window(vec2 uv) { return 1.0 - umbriel_sample(uv); }", "participant-rejected");
	wlr_scene_node_set_animation(&a->node, FX_SLOT_WINDOW, in_place, &parameters);
	ok &= check(fx_scene_participant_admit_for_test(&a->node) == FX_SCENE_PARTICIPANT_IN_PLACE,
		"legacy in-place shader explicitly rejected");
	wlr_scene_node_set_animation(&a->node, FX_SLOT_WINDOW, NULL, NULL);
	ok &= check(fx_scene_participant_admit_for_test(&a->node) == FX_SCENE_PARTICIPANT_SUPPORTED,
		"plain participant readmitted after selected effect changes");
	struct wlr_scene_blur *blur = wlr_scene_blur_create(&scene->tree, 4, 4);
	ok &= check(fx_scene_participant_admit_for_test(&blur->node) == FX_SCENE_PARTICIPANT_BLUR,
		"backdrop blur explicitly rejected");
	wlr_scene_node_destroy(&b->node);
	uint8_t retained[32 * 32 * 4];
	ok &= sources[3] && read_buffer(fixture, sources[3], DRM_FORMAT_ARGB8888, 32 * 4, retained) &&
		check(retained[(14 * 32 + 16) * 4 + 1] > 100 && retained[(14 * 32 + 16) * 4 + 3] > 100,
			"closing content survives immediate source destruction");
	wlr_scene_node_destroy(&scene->tree.node);
	fx_effect_shader_unref(shader);
	fx_effect_shader_unref(in_place);
	for (unsigned i = 0; i < 6; i++) {
		if (sources[i]) {
			wlr_buffer_drop(sources[i]);
		}
	}
	if (composed) {
		wlr_buffer_drop(composed);
	}
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fixture_finish(&fixture);
		return 77;
	}
	bool ok = experiment(&fixture, false) && experiment(&fixture, true);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
