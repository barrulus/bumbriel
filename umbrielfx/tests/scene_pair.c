// Editable authored pair stages, independent of source capture/lifecycle gates.
#include "render_fixture.h"
#include "render/fx_renderer/scene_program.h"

#define WIDTH 128
#define HEIGHT 64
#define BYTES (WIDTH * HEIGHT * 4)

static char *read_stage(const char *name) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s", SCENE_PAIR_FIXTURE_DIR, name);
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

static bool same_pixels(const uint8_t *a, const uint8_t *b) {
	for (unsigned i = 0; i < BYTES; i++) {
		if (abs((int)a[i] - b[i]) > 1) {
			return false;
		}
	}
	return true;
}

static bool render(struct fixture *fixture, struct fx_scene_program *program,
		struct fx_scene_target *target, struct wlr_buffer *buffer,
		struct fx_scene_frame *frame, const struct fx_scene_input pair[2], uint8_t *pixels) {
	return fx_scene_program_render(program, target, frame, pair) &&
		read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, WIDTH * 4, pixels);
}

static bool experiment(struct fixture *fixture, const char *name, bool melt) {
	char *source = read_stage(name);
	if (!check(source != NULL, "read editable authored pair fixture")) {
		return false;
	}
	struct fx_scene_sources sources = {.fragment = source};
	struct fx_scene_program *program = fx_scene_program_create(fixture->renderer, FX_SCENE_PAIR, &sources, NULL, 0);
	free(source);
	struct wlr_buffer *buffer = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, WIDTH, HEIGHT);
	struct fx_scene_target *target = buffer ? fx_scene_target_create(fixture->renderer, buffer) : NULL;
	uint8_t from[BYTES], to[BYTES], pixels[BYTES], held[BYTES];
	for (unsigned y = 0; y < HEIGHT; y++) {
		for (unsigned x = 0; x < WIDTH; x++) {
			unsigned i = (y * WIDTH + x) * 4;
			from[i] = 180 + y;
			from[i + 1] = (2 * x + 3 * y) % 256;
			from[i + 2] = 0;
			from[i + 3] = 255;
			to[i] = 0;
			to[i + 1] = (3 * x + y) % 256;
			to[i + 2] = 200 + y / 2;
			to[i + 3] = 255;
		}
	}
	struct wlr_texture *from_texture = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888,
		WIDTH * 4, WIDTH, HEIGHT, from);
	struct wlr_texture *to_texture = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888,
		WIDTH * 4, WIDTH, HEIGHT, to);
	struct fx_scene_input pair[2] = {{.texture = from_texture}, {.texture = to_texture}};
	bool ok = check(program && target && from_texture && to_texture, "prepare authored pair resources");
	struct fx_scene_frame frame = {.output_size = {WIDTH, HEIGHT}, .scale = 1, .scene_count = 2,
		.random_seed = {0.21f, 0.67f, 0.13f, 0.89f}};
	for (unsigned axis = 0; ok && axis < 2; axis++) {
		frame.axis[0] = axis == 0;
		frame.axis[1] = axis == 1;
		for (unsigned reverse = 0; ok && reverse < 2; reverse++) {
			frame.direction = reverse ? -1 : 1;
			{
				frame.progress = 0;
				ok &= render(fixture, program, target, buffer, &frame, pair, pixels) &&
					check(same_pixels(pixels, from), "exact outgoing endpoint");
				frame.progress = 1;
				ok &= render(fixture, program, target, buffer, &frame, pair, pixels) &&
					check(same_pixels(pixels, to), "exact destination endpoint");
			}
			frame.progress = 0.45f;
			ok &= render(fixture, program, target, buffer, &frame, pair, held);
			unsigned revealed = 0, outgoing = 0, displaced = 0;
			for (unsigned i = 0; ok && i < BYTES; i += 4) {
				if (held[i] < 2) {
					revealed++;
					ok &= check(abs((int)held[i + 1] - to[i + 1]) <= 1 &&
						abs((int)held[i + 2] - to[i + 2]) <= 1,
						"revealed destination retains its original full-scene coordinates");
				} else {
					outgoing++;
					displaced += abs((int)held[i] - from[i]) > 3 || abs((int)held[i + 1] - from[i + 1]) > 3;
				}
				ok &= check(held[i + 3] == 255, "complete desktop stays opaque during pair composition");
			}
			ok &= check(revealed > 100 && outgoing > 100, "both independent scenes contribute at intermediate progress");
			if (melt) {
				ok &= check(displaced > 100, "melt displaces outgoing content rather than merely masking it");
			}
			frame.progress = 0.8f;
			ok &= render(fixture, program, target, buffer, &frame, pair, pixels);
			frame.progress = 0.45f;
			ok &= render(fixture, program, target, buffer, &frame, pair, pixels) &&
				check(same_pixels(pixels, held), "reversing held-input progress retraces the same authored shape");
			// A source/destination alias must fail the exact destination oracle.
			struct fx_scene_input alias[2] = {pair[0], pair[0]};
			frame.progress = 1;
			ok &= render(fixture, program, target, buffer, &frame, alias, pixels) &&
				check(!same_pixels(pixels, to), "distinct-scene endpoint assertion detects aliased inputs");
		}
	}
	if (from_texture) {
		wlr_texture_destroy(from_texture);
	}
	if (to_texture) {
		wlr_texture_destroy(to_texture);
	}
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
	bool ok = experiment(&fixture, "wipe/shader.glsl", false) && experiment(&fixture, "melt/shader.glsl", true) && experiment(&fixture, "iris/shader.glsl", false);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
