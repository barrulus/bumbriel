#include "render_fixture.h"
#include <wlr/util/transform.h>
#include "render/fx_renderer/scene_program.h"

static bool values(struct fixture *fixture, struct wlr_buffer *buffer, bool working, float *result) {
	unsigned count = buffer->width * buffer->height * 4;
	if (working) {
		uint16_t pixels[count];
		if (!read_buffer(fixture, buffer, DRM_FORMAT_ABGR16161616F, buffer->width * 8, pixels)) return false;
		for (unsigned i = 0; i < count; i++) {
			int exponent = (pixels[i] >> 10) & 31;
			float value = exponent ? ldexpf(1 + (pixels[i] & 1023) / 1024.0f, exponent - 15) : ldexpf(pixels[i] & 1023, -24);
			result[i] = pixels[i] & 0x8000 ? -value : value;
		}
	} else {
		uint8_t pixels[count];
		if (!read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, buffer->width * 4, pixels)) return false;
		for (unsigned i = 0; i < count; i++) result[i] = pixels[i] / 255.0f;
	}
	return true;
}

static bool pattern(struct fixture *fixture, struct wlr_buffer *buffer, unsigned transform, bool second, bool working) {
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
	if (!pass) return false;
	int width = buffer->width, height = buffer->height;
	wlr_output_transform_coords(transform, &width, &height);
	glEnable(GL_SCISSOR_TEST);
	for (int y = 0; y < buffer->height; y++) for (int x = 0; x < buffer->width; x++) {
		struct wlr_box point = {x, y, 1, 1};
		wlr_box_transform(&point, &point, transform, buffer->width, buffer->height);
		float r = point.x < width / 2 ? (working ? 2 : 0.4f) : 0.2f;
		float g = point.y < height / 2 ? 0.3f : 0.1f;
		float b = second ? 0.3f : 0.05f;
		glScissor(x, y, 1, 1);
		glClearColor(second ? g : r, second ? r : g, b, second ? 0.75f : 0.5f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glDisable(GL_SCISSOR_TEST);
	return wlr_render_pass_submit(pass);
}

static float sample(const float *pixels, int width, int height, float x, float y, unsigned channel) {
	int ix = floorf(x), iy = floorf(y);
	float sum = 0;
	for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
		int px = ix + i, py = iy + j;
		px = px < 0 ? 0 : (px >= width ? width - 1 : px);
		py = py < 0 ? 0 : (py >= height ? height - 1 : py);
		sum += pixels[(py * width + px) * 4 + channel] * (i ? x - ix : 1 - x + ix) * (j ? y - iy : 1 - y + iy);
	}
	return sum;
}

static bool experiment(struct fixture *fixture, bool working) {
	struct wlr_buffer *a = create_output_buffer(fixture, working ? DRM_FORMAT_ABGR16161616F : DRM_FORMAT_ARGB8888, 8, 4);
	struct wlr_buffer *b = fx_scene_buffer_create(fixture->renderer, fixture->allocator, 16, 8, working);
	struct wlr_buffer *buffer = fx_scene_buffer_create(fixture->renderer, fixture->allocator, 16, 8, working);
	struct fx_scene_target *target = fx_scene_target_create(fixture->renderer, buffer, false);
	bool ok = a && b && target;
	const float weights[] = {0, 0.25f, 0.5f, 0.99f, 1};
	for (unsigned transform = 0; ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
		ok &= pattern(fixture, a, transform, false, working) && pattern(fixture, b, transform, true, working);
		float pa[8 * 4 * 4], pb[16 * 8 * 4], out[16 * 8 * 4];
		ok &= values(fixture, a, working, pa) && values(fixture, b, working, pb);
		struct fx_scene_input first = {.texture = wlr_texture_from_buffer(fixture->renderer, a)};
		struct fx_scene_input second = {.texture = wlr_texture_from_buffer(fixture->renderer, b)};
		for (unsigned w = 0; ok && w < sizeof(weights) / sizeof(weights[0]); w++) {
			ok &= fx_scene_target_blend(target, &first, &second, weights[w], working) && values(fixture, buffer, working, out);
			for (unsigned y = 0; ok && y < 8; y++) for (unsigned x = 0; ok && x < 16; x++) for (unsigned c = 0; c < 4; c++) {
				unsigned index = (y * 16 + x) * 4 + c;
				float expected = sample(pa, 8, 4, (x + 0.5f) / 2 - 0.5f, (y + 0.5f) / 2 - 0.5f, c) * (1 - weights[w]) + pb[index] * weights[w];
				if (!check(fabsf(out[index] - expected) <= (working ? 0.003f : 1.1f / 255), "physical landing blend equals independent bilinear premultiplied sum")) {
					fprintf(stderr, "working=%d transform=%u weight=%f xy=%u,%u c=%u actual=%f expected=%f\n", working, transform, weights[w], x, y, c, out[index], expected);
					ok = false;
					break;
				}
			}
		}
		const float nonidentity[9] = {-1, 0, 0, 0, 1, 0, 1, 0, 1};
		first.sample_matrix = nonidentity;
		ok &= check(!fx_scene_target_blend(target, &first, &second, 0.5f, working), "unadmitted sample transform rejects");
		first.sample_matrix = NULL;
		ok &= check(!fx_scene_target_blend(target, &first, &second, 0.5f, !working), "working-space mismatch rejects");
		wlr_texture_destroy(first.texture);
		wlr_texture_destroy(second.texture);
	}
	fx_scene_target_destroy(target);
	wlr_buffer_drop(a); wlr_buffer_drop(b); wlr_buffer_drop(buffer);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) { fixture_finish(&fixture); return 77; }
	bool ok = experiment(&fixture, false) && experiment(&fixture, true);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}
