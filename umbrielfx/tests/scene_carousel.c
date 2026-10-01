// Editable projected authoring contract. Live workspace capture is tested separately.
#include "render_fixture.h"
#include "render/fx_renderer/scene_program.h"

#define WIDTH 128
#define HEIGHT 96
#define BYTES (WIDTH * HEIGHT * 4)

static char *read_stage(const char *extension) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/carousel/shader.%s", SCENE_CAROUSEL_FIXTURE_DIR, extension);
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
	char *vertex = read_stage("vert"), *fragment = read_stage("frag");
	const struct fx_scene_sources sources = {.vertex = vertex, .fragment = fragment};
	struct fx_scene_program *program = vertex && fragment ? fx_scene_program_create(fixture->renderer,
		FX_SCENE_SET, &sources, NULL, 0) : NULL;
	free(vertex);
	free(fragment);
	struct wlr_buffer *buffer = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, WIDTH, HEIGHT);
	struct fx_scene_target *target = buffer ? fx_scene_target_create(fixture->renderer, buffer, true) : NULL;
	struct fx_scene_mesh quad = {0};
	bool ok = check(program && target && fx_scene_mesh_create(&quad, 1, 1, 64), "prepare editable carousel");
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
	struct fx_scene_frame frame = {.output_size = {WIDTH, HEIGHT}, .scale = 1};
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
				ok &= render(fixture, program, target, buffer, &frame, draws, pixels);
				for (unsigned p = 0; ok && p < BYTES; p += 4) {
					ok &= check(memcmp(pixels + p, colors[selected], 4) == 0,
						"each face lands on its complete native-size source at either audio extreme");
				}
			}
		}
		if (!ok) {
			break;
		}
		frame.progress = 1;
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
