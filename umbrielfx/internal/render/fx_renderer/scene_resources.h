#ifndef UMBRIELFX_SCENE_RESOURCES_H
#define UMBRIELFX_SCENE_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Internal C0 primitives. These are not a released shader or compositor ABI.
#define FX_SCENE_MAX_FACES 64u
#define FX_SCENE_MAX_PARTICIPANTS 32u
#define FX_SCENE_MAX_DRAWS 128u
#define FX_SCENE_MAX_GRID 64u
#define FX_SCENE_MAX_VERTICES 131072u
#define FX_SCENE_OUTPUT_BUDGET (UINT64_C(256) * 1024 * 1024)
#define FX_SCENE_TOTAL_BUDGET (UINT64_C(512) * 1024 * 1024)

enum fx_scene_admission {
	FX_SCENE_ADMITTED,
	FX_SCENE_INVALID,
	FX_SCENE_UNSUPPORTED,
	FX_SCENE_RESOURCE_BUDGET,
};

struct fx_scene_resource_request {
	uint32_t width, height, faces;
	uint32_t color_bytes; // RGBA8 = 4, RGBA16F = 8
	uint32_t roles; // 1 when display/capture alias; 2 when distinct
	uint32_t scratch_images; // full resolution, per role
	uint32_t depth_bytes; // full resolution, per role; 0, 2 or 4
	uint32_t texture_limit;
	uint64_t held_bytes; // existing resources retained by this lease
	uint64_t allocation_overhead; // allocator padding/metadata, for ALL images
	bool downscale_faces; // workspace_set only; pair/participants stay native
};

struct fx_scene_resource_plan {
	uint32_t face_width, face_height, divisor, faces, roles;
	uint64_t face_bytes, landing_bytes, scratch_bytes, depth_bytes, total_bytes;
};

// Includes native landing imagery in every candidate and never truncates faces.
// On failure, out is zeroed. Limits are available bytes, not total arena sizes.
enum fx_scene_admission fx_scene_plan_resources(
	const struct fx_scene_resource_request *request, uint64_t output_available,
	uint64_t aggregate_available, struct fx_scene_resource_plan *out);

struct fx_scene_resource_pool {
	uint64_t limit, used;
};

struct fx_scene_reservation {
	struct fx_scene_resource_pool *output, *aggregate;
	uint64_t bytes;
};

// Single-threaded compositor reservation: both pools change or neither does.
// The caller releases on allocation failure before acquiring presentation.
bool fx_scene_reserve(struct fx_scene_reservation *reservation,
	struct fx_scene_resource_pool *output, struct fx_scene_resource_pool *aggregate,
	uint64_t bytes);
void fx_scene_release(struct fx_scene_reservation *reservation);

struct fx_scene_mesh {
	float *uv; // top-left normalized, two floats per vertex
	uint16_t *indices;
	uint32_t vertices, index_count;
};

bool fx_scene_mesh_create(struct fx_scene_mesh *mesh, uint32_t columns, uint32_t rows,
	uint32_t draws);
void fx_scene_mesh_finish(struct fx_scene_mesh *mesh);

struct fx_scene_box {
	double x, y, width, height;
};

// Viewport and content remain logical pixels. The canvas always keeps the
// output aspect; fit_all expands the capture extent, never the face geometry.
bool fx_scene_frame_extent(const struct fx_scene_box *viewport,
	const struct fx_scene_box *content, size_t count, bool fit_all,
	struct fx_scene_box *extent);

#endif
