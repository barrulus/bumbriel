// Authored residuals and endpoint contracts, independent of participant capture.
#include "render_fixture.h"
#include "render/fx_renderer/scene_program.h"

#define WIDTH 128
#define HEIGHT 96
#define BYTES (WIDTH * HEIGHT * 4)

static char *read_stage(const char *name, const char *suffix) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s/%s", SCENE_WINDOWS_FIXTURE_DIR, name, suffix);
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
		struct fx_scene_target *target, struct fx_scene_target *composed,
		struct wlr_buffer *buffer, const struct fx_scene_frame *frame,
		const struct fx_scene_draw *draws, unsigned count, uint8_t *pixels) {
	return check(fx_scene_program_render(program, target, composed, frame, NULL, draws, count),
		"render valid authored window descriptors") &&
		check(read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, WIDTH * 4, pixels), "read authored window pixels");
}

static bool experiment(struct fixture *fixture, const char *name) {
	char *common = read_stage(name, "common.glsl");
	char *vertex = read_stage(name, "shader.vert");
	char *fragment = read_stage(name, "shader.frag");
	char *composite = read_stage(name, "composite.glsl");
	struct fx_scene_sources sources = {
		.common = common, .vertex = vertex, .fragment = fragment, .composite = composite,
	};
	bool ok = check(common && vertex && fragment && composite, "read editable window stages");
	struct fx_scene_program *program = ok ? fx_scene_program_create(fixture->renderer,
		FX_SCENE_WINDOWS, &sources, NULL, 0) : NULL;
	sources.composite = NULL;
	struct fx_scene_program *without_composite = ok ? fx_scene_program_create(fixture->renderer,
		FX_SCENE_WINDOWS, &sources, NULL, 0) : NULL;
	free(common);
	free(vertex);
	free(fragment);
	free(composite);
	const struct fx_scene_sources native_sources = {
		.vertex = "vec4 transition_vertex(vec2 uv) { return vec4("
			"(umbriel_capture_extent.xy+uv*umbriel_capture_extent.zw)/umbriel_output_size*2.0-1.0,0.0,1.0); }",
		.fragment = "vec4 transition_fragment(vec2 uv,vec2 output_uv) {return umbriel_sample_item(uv);}",
	};
	struct fx_scene_program *native = fx_scene_program_create(fixture->renderer,
		FX_SCENE_WINDOWS, &native_sources, NULL, 0);
	struct wlr_buffer *buffer = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, WIDTH, HEIGHT);
	struct wlr_buffer *scratch = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, WIDTH, HEIGHT);
	struct fx_scene_target *target = buffer ? fx_scene_target_create(fixture->renderer, buffer, false) : NULL;
	struct fx_scene_target *composed = scratch ? fx_scene_target_create(fixture->renderer, scratch, false) : NULL;
	struct fx_scene_mesh quad = {0}, grid = {0};
	ok &= check(program && without_composite && native && target && composed &&
		fx_scene_mesh_create(&quad, 1, 1, 4) && fx_scene_mesh_create(&grid, 16, 16, 4),
		"prepare authored window bundle and bounded geometry");
	// Background, two translucent neighbours, and a translucent target. The
	// explicit target token deliberately differs from its draw ordinal.
	const uint8_t colors[4][4] = {{24, 32, 48, 255}, {96, 16, 8, 128}, {8, 72, 96, 160}, {100, 92, 8, 128}};
	const float boxes[4][4] = {{0, 0, WIDTH, HEIGHT}, {8, 14, 44, 56}, {75, 18, 42, 60}, {42, 32, 44, 42}};
	struct wlr_texture *textures[4] = {0};
	struct fx_scene_draw draws[4] = {0};
	for (unsigned i = 0; ok && i < 4; i++) {
		int padding = i ? 4 : 0;
		int width = boxes[i][2] + padding*2, height = boxes[i][3] + padding*2;
		uint8_t *canvas = calloc((size_t)width*height, 4);
		for (int y = padding; y < height-padding; y++) {
			for (int x = padding; x < width-padding; x++) memcpy(&canvas[(y*width+x)*4], colors[i], 4);
		}
		textures[i] = wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888, width*4, width, height, canvas);
		free(canvas);
		draws[i].input.texture = textures[i];
		draws[i].mesh = i ? &grid : &quad;
		draws[i].item.kind = i ? FX_SCENE_CONTENT : FX_SCENE_STATIC;
		draws[i].item.token = i ? 20 + i : 0;
		draws[i].item.ordinal = i;
		draws[i].item.native_opacity = colors[i][3] / 255.0f;
		draws[i].item.motion_progress = 0.35f;
		memcpy(draws[i].item.current_box, boxes[i], sizeof(boxes[i]));
		memcpy(draws[i].item.capture_extent, boxes[i], sizeof(boxes[i]));
		draws[i].item.capture_extent[0] -= padding;
		draws[i].item.capture_extent[1] -= padding;
		draws[i].item.capture_extent[2] += padding*2;
		draws[i].item.capture_extent[3] += padding*2;
		memcpy(draws[i].item.source_box, boxes[i], sizeof(boxes[i]));
		memcpy(draws[i].item.destination_box, boxes[i], sizeof(boxes[i]));
		draws[i].item.destination_box[0] += 10;
		ok &= check(textures[i] != NULL, "independent premultiplied participant inputs");
	}
	struct fx_scene_frame frame = {.output_size = {WIDTH, HEIGHT}, .scale = 1,
		.target_token = 23, .random_seed = {0.21f, 0.67f, 0.13f, 0.89f}};
	uint8_t pixels[BYTES] = {0}, ordinary[BYTES] = {0}, held[BYTES] = {0}, control[BYTES] = {0};
	for (unsigned close = 0; ok && close < 2; close++) {
		frame.direction = close ? -1 : 1;
		for (unsigned endpoint = 0; ok && endpoint < 2; endpoint++) {
			frame.progress = endpoint;
			// Current native reflow need not be at source or destination when
			// the shorter opening/closing deadline completes.
			draws[1].item.current_box[0] = endpoint ? 13 : 8;
			draws[1].item.capture_extent[0] = draws[1].item.current_box[0] - 4;
			unsigned native_count = (close == endpoint) ? 3 : 4;
			ok &= render(fixture, native, target, NULL, buffer, &frame, draws, native_count, ordinary);
			unsigned neighbour = (30 * WIDTH + 20) * 4;
			ok &= check(ordinary[0] == 24 && ordinary[1] == 32 && ordinary[2] == 48 && ordinary[3] == 255 &&
				abs((int)ordinary[neighbour] - 108) <= 1 &&
				abs((int)ordinary[neighbour + 1] - 32) <= 1 &&
				abs((int)ordinary[neighbour + 2] - 32) <= 1 && ordinary[neighbour + 3] == 255,
				"native reference retains background and one premultiplied translucent neighbour");
			for (unsigned sound = 0; ok && sound < 2; sound++) {
				frame.audio_levels[0] = 1;
				frame.audio_levels[3] = sound;
				ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, pixels) &&
					check(same_pixels(pixels, ordinary), "opening/closing endpoint is exact current native scene at either audio extreme");
			}
		}
		if (!ok) {
			break;
		}
		frame.progress = 0.45f;
		frame.audio_levels[3] = 0;
		ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, held) &&
			render(fixture, without_composite, target, NULL, buffer, &frame, draws, 4, control) &&
			check(!same_pixels(held, control), "authored output-wide stage affects intermediate pixels");
		if (!ok) {
			break;
		}
		unsigned outside_changes = 0;
		for (unsigned y = 0; y < 10; y++) {
			for (unsigned x = 0; x < WIDTH; x++) {
				unsigned i = (y * WIDTH + x) * 4;
				outside_changes += abs((int)held[i + 2] - control[i + 2]) > 1;
			}
		}
		fprintf(stderr, "%s close=%u outside-window pixels=%u\n", name, close, outside_changes);
		ok &= check(outside_changes > 8, "output-wide shading extends beyond every window");
		frame.progress = 0.8f;
		ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, pixels);
		frame.progress = 0.45f;
		ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, pixels) &&
			check(same_pixels(pixels, held), "analytic window stages retrace held-input progress");
		frame.audio_levels[3] = 1;
		ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, pixels) &&
			check(!same_pixels(pixels, held), "shared audio changes intermediate window presentation");
		frame.audio_levels[3] = 0;
		frame.target_token = 2;
		ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, pixels) &&
			check(!same_pixels(pixels, held), "target identity assertion distinguishes owner token from draw ordinal");
		frame.target_token = 23;
		// Holding the same presentation metadata but drawing only four corner
		// vertices must lose the authored interior deformation.
		for (unsigned i = 1; i < 4; i++) {
			draws[i].mesh = &quad;
		}
		ok &= render(fixture, program, target, composed, buffer, &frame, draws, 4, pixels) &&
			check(!same_pixels(pixels, held), "quad control detects authored grid deformation");
		for (unsigned i = 1; i < 4; i++) {
			draws[i].mesh = &grid;
		}
	}
	for (unsigned i = 0; i < 4; i++) {
		if (textures[i]) {
			wlr_texture_destroy(textures[i]);
		}
	}
	fx_scene_mesh_finish(&quad);
	fx_scene_mesh_finish(&grid);
	fx_scene_target_destroy(target);
	fx_scene_target_destroy(composed);
	if (buffer) {
		wlr_buffer_drop(buffer);
	}
	if (scratch) {
		wlr_buffer_drop(scratch);
	}
	fx_scene_program_unref(program);
	fx_scene_program_unref(without_composite);
	fx_scene_program_unref(native);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fixture_finish(&fixture);
		return 77;
	}
	bool ok = experiment(&fixture, "water") && experiment(&fixture, "portal");
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
