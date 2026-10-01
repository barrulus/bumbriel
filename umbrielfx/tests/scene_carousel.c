// Editable projected authoring contract. Live workspace capture is tested separately.
#include "render_fixture.h"
#include "render/fx_renderer/scene_program.h"

#define WIDTH 128
#define HEIGHT 96
#define BYTES (WIDTH * HEIGHT * 4)

static char *read_stage(const char *name) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/carousel/%s", SCENE_CAROUSEL_FIXTURE_DIR, name);
	FILE *file = fopen(path, "rb");
	if (!file) {
		return NULL;
	}
	char *source = calloc(8192, 1);
	if (!source) {
		fclose(file);
		return NULL;
	}
	size_t size = fread(source, 1, 8191, file);
	bool ok = !ferror(file) && feof(file) && size > 0;
	fclose(file);
	if (!ok) {
		free(source);
		return NULL;
	}
	return source;
}

static bool render(struct fixture *fixture, struct fx_scene_program *program,
		struct fx_scene_target *target, struct wlr_buffer *buffer,
		const struct fx_scene_frame *frame, const struct fx_scene_draw *draws, uint8_t *pixels) {
	return check(fx_scene_program_render(program, target, NULL, frame, NULL, draws, frame->scene_count),
		"render complete authored carousel inventory") &&
		check(read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, WIDTH * 4, pixels), "read carousel pixels");
}

static bool experiment(struct fixture *fixture) {
	char *vertex = read_stage("shader.vert"), *fragment = read_stage("shader.frag");
	char *backdrop = read_stage("backdrop.glsl");
	const struct fx_scene_sources sources = {.vertex = vertex, .fragment = fragment, .backdrop = backdrop};
	const struct fx_scene_parameter parameters[] = {
		{.name = "max_elevation_degrees", .components = 1, .value = {75}},
		{.name = "backdrop_style", .components = 1, .value = {2}},
		{.name = "backdrop_opacity", .components = 1, .value = {1}},
		{.name = "backdrop_brightness", .components = 1, .value = {1}},
		{.name = "backdrop_color", .components = 3, .value = {.0667f, .0667f, .1059f}},
		{.name = "nebula_color", .components = 3, .value = {.7961f, .6510f, .9686f}},
		{.name = "nebula_accent", .components = 3, .value = {.5804f, .8863f, .8353f}},
		{.name = "star_color", .components = 3, .value = {.9804f, .7020f, .5294f}},
		{.name = "nebula_strength", .components = 1, .value = {1}},
		{.name = "star_density", .components = 1, .value = {1}},
	};
	struct fx_scene_program *program = vertex && fragment && backdrop ? fx_scene_program_create(fixture->renderer,
		FX_SCENE_SET, &sources, parameters, sizeof(parameters) / sizeof(parameters[0])) : NULL;
	free(vertex);
	free(fragment);
	free(backdrop);
	struct wlr_buffer *buffer = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, WIDTH, HEIGHT);
	struct fx_scene_target *target = buffer ? fx_scene_target_create(fixture->renderer, buffer, true) : NULL;
	struct fx_scene_mesh quad = {0};
	bool ok = check(program && target && fx_scene_mesh_create(&quad, 1, 1, 64), "prepare editable carousel");
	ok &= check(fx_scene_program_supports_picking(program), "backdrop preserves geometry picking");
	ok &= check(!fx_scene_program_reads_time(program), "static universe does not demand an animation clock");
	struct wlr_texture *textures[64] = {0};
	struct fx_scene_draw draws[64] = {0};
	uint8_t colors[64][4];
	for (unsigned i = 0; ok && i < 64; i++) {
		colors[i][0] = 16 + i * 3;
		colors[i][1] = 224 - i * 3;
		colors[i][2] = 32 + i;
		colors[i][3] = 255;
		textures[i] = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888, 4, 1, 1, colors[i]);
		draws[i].input.texture = textures[i];
		draws[i].mesh = &quad;
		draws[i].item.kind = FX_SCENE_FACE;
		draws[i].item.token = i + 1;
		draws[i].item.ordinal = i;
		ok &= check(textures[i] != NULL, "one numbered source for every workspace face");
	}
	struct fx_scene_frame frame = {.output_size = {WIDTH, HEIGHT}, .scale = 1,
		.pointer = {0.5f, 0.5f}, .zoom = 1};
	uint8_t pixels[BYTES] = {0}, held[BYTES] = {0};
	const unsigned counts[] = {1, 2, 3, 4, 5, 8, 64};
	for (unsigned c = 0; ok && c < sizeof(counts) / sizeof(counts[0]); c++) {
		frame.scene_count = counts[c];
		for (unsigned selected = 0; ok && selected < counts[c]; selected++) {
			frame.navigation_position = selected;
			for (unsigned sound = 0; ok && sound < 2; sound++) {
				frame.audio_levels[0] = 1;
				frame.audio_levels[3] = sound;
				frame.progress = 0;
				frame.zoom = sound ? 3.0f : 0.25f;
				ok &= render(fixture, program, target, buffer, &frame, draws, pixels);
				for (unsigned p = 0; ok && p < BYTES; p += 4) {
					ok &= check(memcmp(pixels + p, colors[selected], 4) == 0,
						"each face lands on its complete native-size source at either audio/zoom extreme");
				}
			}
		}
		if (!ok) {
			break;
		}
		frame.progress = 1;
		frame.zoom = 1;
		frame.navigation_position = counts[c] > 1 ? 0.5f : 0;
		frame.audio_levels[3] = 0;
		ok &= render(fixture, program, target, buffer, &frame, draws, held);
		unsigned visible[64] = {0};
		for (unsigned p = 0; p < BYTES; p += 4) {
			for (unsigned i = 0; i < counts[c]; i++) {
				visible[i] += memcmp(held + p, colors[i], 4) == 0;
			}
		}
		ok &= check(visible[0] > 32 && (counts[c] == 1 || visible[1] > 32),
			"fractional navigation keeps adjacent equal canvases visible, including the nondegenerate two-face case");
		frame.navigation_position += 0.25f;
		ok &= render(fixture, program, target, buffer, &frame, draws, pixels);
		frame.navigation_position -= 0.25f;
		ok &= render(fixture, program, target, buffer, &frame, draws, pixels) &&
			check(memcmp(pixels, held, BYTES) == 0, "reversed held navigation reproduces the same scene");
		frame.audio_levels[3] = 1;
		ok &= render(fixture, program, target, buffer, &frame, draws, pixels) &&
			check(memcmp(pixels, held, BYTES) != 0, "audio changes held geometry without changing navigation identity");
		frame.audio_levels[3] = 0;
		frame.zoom = 0.5f;
		ok &= render(fixture, program, target, buffer, &frame, draws, pixels) &&
			check(memcmp(pixels, held, BYTES) != 0, "zoom changes held geometry without changing navigation identity");
	}
	if (ok) {
		frame.scene_count = 6;
		frame.navigation_position = 0;
		frame.zoom = 1;
		ok &= render(fixture, program, target, buffer, &frame, draws, pixels);
		ok &= check(pixels[3] == 255, "universe fills the uncovered output");
		struct fx_scene_picker *picker = fx_scene_picker_create(fixture->renderer);
		struct fx_scene_pick hit = {0};
		ok &= check(picker && fx_scene_program_pick(program, picker, target, &frame, draws, 6,
			WIDTH / 2, HEIGHT / 2, &hit) == FX_SCENE_PICK_HIT && hit.ordinal == 0,
			"opaque workspace geometry remains pickable over the universe");
		ok &= check(picker && fx_scene_program_pick(program, picker, target, &frame, draws, 6,
			0, 0, &hit) == FX_SCENE_PICK_MISS, "backdrop pixels never masquerade as workspace faces");
		fx_scene_picker_destroy(picker);
	}
	for (unsigned i = 0; i < 64; i++) {
		if (textures[i]) {
			wlr_texture_destroy(textures[i]);
		}
	}
	fx_scene_mesh_finish(&quad);
	fx_scene_target_destroy(target);
	if (buffer) {
		wlr_buffer_drop(buffer);
	}
	fx_scene_program_unref(program);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fixture_finish(&fixture);
		return 77;
	}
	bool ok = experiment(&fixture);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
