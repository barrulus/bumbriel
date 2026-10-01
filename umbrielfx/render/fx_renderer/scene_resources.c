#include "render/fx_renderer/scene_resources.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static bool add(uint64_t *sum, uint64_t value) {
	if (UINT64_MAX - *sum < value) {
		return false;
	}
	*sum += value;
	return true;
}

static bool image_bytes(uint32_t width, uint32_t height, uint32_t bytes,
		uint32_t count, uint64_t *out) {
	uint64_t pixels = (uint64_t)width * height;
	uint64_t multiplier = (uint64_t)bytes * count;
	if (multiplier && pixels > UINT64_MAX / multiplier) {
		return false;
	}
	*out = pixels * multiplier;
	return true;
}

enum fx_scene_admission fx_scene_plan_resources(
		const struct fx_scene_resource_request *r, uint64_t output_available,
		uint64_t aggregate_available, struct fx_scene_resource_plan *out) {
	if (!out) {
		return FX_SCENE_INVALID;
	}
	*out = (struct fx_scene_resource_plan){0};
	if (!r || !r->width || !r->height || !r->faces ||
			r->faces > FX_SCENE_MAX_FACES || (r->roles != 1 && r->roles != 2) ||
			(r->color_bytes != 4 && r->color_bytes != 8) ||
			(r->depth_bytes != 0 && r->depth_bytes != 2 && r->depth_bytes != 4) ||
			r->scratch_images > FX_SCENE_MAX_DRAWS) {
		return FX_SCENE_INVALID;
	}
	// Even reduced faces need the full-resolution landing and scratch targets.
	if (r->width > r->texture_limit || r->height > r->texture_limit) {
		return FX_SCENE_UNSUPPORTED;
	}
	for (uint32_t divisor = 1; divisor <= (r->downscale_faces ? 4u : 1u); divisor *= 2) {
		struct fx_scene_resource_plan p = {
			.face_width = r->width / divisor + (r->width % divisor != 0),
			.face_height = r->height / divisor + (r->height % divisor != 0),
			.divisor = divisor, .faces = r->faces, .roles = r->roles,
			.total_bytes = r->held_bytes,
		};
		bool valid = image_bytes(p.face_width, p.face_height, r->color_bytes,
			r->faces * r->roles, &p.face_bytes) &&
			image_bytes(r->width, r->height, r->color_bytes, r->roles, &p.landing_bytes) &&
			image_bytes(r->width, r->height, r->color_bytes,
				r->scratch_images * r->roles, &p.scratch_bytes) &&
			image_bytes(r->width, r->height, r->depth_bytes, r->roles, &p.depth_bytes) &&
			add(&p.total_bytes, p.face_bytes) && add(&p.total_bytes, p.landing_bytes) &&
			add(&p.total_bytes, p.scratch_bytes) && add(&p.total_bytes, p.depth_bytes) &&
			add(&p.total_bytes, r->allocation_overhead);
		if (valid && p.total_bytes <= output_available && p.total_bytes <= aggregate_available) {
			*out = p;
			return FX_SCENE_ADMITTED;
		}
	}
	return FX_SCENE_RESOURCE_BUDGET;
}

bool fx_scene_reserve(struct fx_scene_reservation *reservation,
		struct fx_scene_resource_pool *output, struct fx_scene_resource_pool *aggregate,
		uint64_t bytes) {
	if (!reservation || reservation->bytes || !output || !aggregate || output == aggregate ||
			!bytes || output->used > output->limit || aggregate->used > aggregate->limit ||
			bytes > output->limit - output->used || bytes > aggregate->limit - aggregate->used) {
		return false;
	}
	output->used += bytes;
	aggregate->used += bytes;
	*reservation = (struct fx_scene_reservation){output, aggregate, bytes};
	return true;
}

void fx_scene_release(struct fx_scene_reservation *reservation) {
	if (!reservation || !reservation->bytes) {
		return;
	}
	assert(reservation->output->used >= reservation->bytes);
	assert(reservation->aggregate->used >= reservation->bytes);
	reservation->output->used -= reservation->bytes;
	reservation->aggregate->used -= reservation->bytes;
	*reservation = (struct fx_scene_reservation){0};
}

bool fx_scene_mesh_create(struct fx_scene_mesh *mesh, uint32_t columns, uint32_t rows,
		uint32_t draws) {
	if (!mesh || mesh->uv || mesh->indices || !columns || !rows || !draws ||
			columns > FX_SCENE_MAX_GRID || rows > FX_SCENE_MAX_GRID || draws > FX_SCENE_MAX_DRAWS) {
		return false;
	}
	uint32_t vertices = (columns + 1) * (rows + 1);
	if (vertices * draws > FX_SCENE_MAX_VERTICES) {
		return false;
	}
	uint32_t indices = columns * rows * 6;
	float *uv = calloc(vertices * 2, sizeof(*uv));
	uint16_t *index = calloc(indices, sizeof(*index));
	if (!uv || !index) {
		free(uv);
		free(index);
		return false;
	}
	for (uint32_t y = 0; y <= rows; y++) {
		for (uint32_t x = 0; x <= columns; x++) {
			uint32_t i = y * (columns + 1) + x;
			uv[2 * i] = (float)x / columns;
			uv[2 * i + 1] = (float)y / rows;
		}
	}
	uint32_t offset = 0;
	for (uint32_t y = 0; y < rows; y++) {
		for (uint32_t x = 0; x < columns; x++) {
			uint16_t a = y * (columns + 1) + x, b = a + 1;
			uint16_t c = a + columns + 1, d = c + 1;
			index[offset++] = a; index[offset++] = c; index[offset++] = b;
			index[offset++] = b; index[offset++] = c; index[offset++] = d;
		}
	}
	*mesh = (struct fx_scene_mesh){uv, index, vertices, indices};
	return true;
}

void fx_scene_mesh_finish(struct fx_scene_mesh *mesh) {
	if (mesh) {
		free(mesh->uv);
		free(mesh->indices);
		*mesh = (struct fx_scene_mesh){0};
	}
}

static bool valid_box(const struct fx_scene_box *box) {
	return isfinite(box->x) && isfinite(box->y) && isfinite(box->width) &&
		isfinite(box->height) && box->width > 0 && box->height > 0 &&
		isfinite(box->x + box->width) && isfinite(box->y + box->height);
}

bool fx_scene_frame_extent(const struct fx_scene_box *viewport,
		const struct fx_scene_box *content, size_t count, bool fit_all,
		struct fx_scene_box *extent) {
	if (!viewport || !extent || !valid_box(viewport) || (count && !content)) {
		return false;
	}
	struct fx_scene_box result = *viewport;
	if (fit_all) {
		double right = result.x + result.width, bottom = result.y + result.height;
		for (size_t i = 0; i < count; i++) {
			if (!valid_box(&content[i])) {
				return false;
			}
			result.x = fmin(result.x, content[i].x);
			result.y = fmin(result.y, content[i].y);
			right = fmax(right, content[i].x + content[i].width);
			bottom = fmax(bottom, content[i].y + content[i].height);
		}
		result.width = right - result.x;
		result.height = bottom - result.y;
		double scale = fmax(result.width / viewport->width, result.height / viewport->height);
		double width = viewport->width * scale, height = viewport->height * scale;
		result.x -= (width - result.width) / 2;
		result.y -= (height - result.height) / 2;
		result.width = width;
		result.height = height;
	}
	if (!valid_box(&result)) {
		return false;
	}
	*extent = result;
	return true;
}
