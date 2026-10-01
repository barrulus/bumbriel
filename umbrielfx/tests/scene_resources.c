#include "render/fx_renderer/scene_resources.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); return false; \
} } while (0)

static bool test_resources(void) {
	struct fx_scene_resource_request request = {
		.width = 3840, .height = 2160, .faces = 64, .color_bytes = 8,
		.roles = 1, .scratch_images = 1, .depth_bytes = 4,
		.texture_limit = 16384, .downscale_faces = true,
	};
	struct fx_scene_resource_plan plan;
	CHECK(fx_scene_plan_resources(&request, FX_SCENE_OUTPUT_BUDGET,
		FX_SCENE_TOTAL_BUDGET, &plan) == FX_SCENE_RESOURCE_BUDGET);
	CHECK(plan.total_bytes == 0 && plan.faces == 0);
	// The specification's lower bound already fails before depth/scratch.
	request.scratch_images = 0;
	request.depth_bytes = 0;
	CHECK(fx_scene_plan_resources(&request, UINT64_MAX, UINT64_MAX, &plan) == FX_SCENE_ADMITTED);
	uint64_t quarter_bound = UINT64_C(960) * 540 * 64 * 8 + UINT64_C(3840) * 2160 * 8;
	CHECK(quarter_bound == 331776000);
	CHECK(fx_scene_plan_resources(&request, quarter_bound, quarter_bound, &plan) == FX_SCENE_ADMITTED);
	CHECK(plan.divisor == 4 && plan.total_bytes == quarter_bound && plan.faces == 64);
	CHECK(fx_scene_plan_resources(&request, quarter_bound - 1, UINT64_MAX, &plan) == FX_SCENE_RESOURCE_BUDGET);
	request.color_bytes = 4;
	request.scratch_images = 1;
	request.depth_bytes = 4;
	CHECK(fx_scene_plan_resources(&request, FX_SCENE_OUTPUT_BUDGET,
		FX_SCENE_TOTAL_BUDGET, &plan) == FX_SCENE_ADMITTED);
	CHECK(plan.faces == 64 && plan.divisor == 4 && plan.landing_bytes == UINT64_C(3840) * 2160 * 4);
	request.roles = 2;
	CHECK(fx_scene_plan_resources(&request, FX_SCENE_OUTPUT_BUDGET,
		FX_SCENE_TOTAL_BUDGET, &plan) == FX_SCENE_RESOURCE_BUDGET);
	// Complete dual-role inventory, including both landings, scratch and depth.
	request.width = 1920;
	request.height = 1080;
	request.allocation_overhead = 4096;
	request.held_bytes = 8192;
	CHECK(fx_scene_plan_resources(&request, FX_SCENE_OUTPUT_BUDGET,
		FX_SCENE_TOTAL_BUDGET, &plan) == FX_SCENE_ADMITTED);
	CHECK(plan.faces == 64 && plan.roles == 2);
	CHECK(plan.total_bytes == plan.face_bytes + plan.landing_bytes +
		plan.scratch_bytes + plan.depth_bytes + 12288);
	uint64_t total = plan.total_bytes;
	CHECK(fx_scene_plan_resources(&request, FX_SCENE_OUTPUT_BUDGET,
		total / 4, &plan) == FX_SCENE_RESOURCE_BUDGET);
	request.downscale_faces = false;
	CHECK(fx_scene_plan_resources(&request, FX_SCENE_OUTPUT_BUDGET,
		FX_SCENE_TOTAL_BUDGET, &plan) == FX_SCENE_RESOURCE_BUDGET);
	request.faces = 65;
	CHECK(fx_scene_plan_resources(&request, UINT64_MAX, UINT64_MAX, &plan) == FX_SCENE_INVALID);
	request.faces = 64;
	request.width = request.height = request.texture_limit = UINT32_MAX;
	CHECK(fx_scene_plan_resources(&request, UINT64_MAX, UINT64_MAX, &plan) == FX_SCENE_RESOURCE_BUDGET);
	request.width = 20000;
	request.texture_limit = 16384;
	CHECK(fx_scene_plan_resources(&request, UINT64_MAX, UINT64_MAX, &plan) == FX_SCENE_UNSUPPORTED);
	return true;
}

static bool test_reservations(void) {
	struct fx_scene_resource_pool aggregate = {500, 0}, a = {256, 0}, b = {256, 0};
	struct fx_scene_reservation first = {0}, second = {0}, rejected = {0};
	CHECK(fx_scene_reserve(&first, &a, &aggregate, 240));
	CHECK(!fx_scene_reserve(&rejected, &a, &aggregate, 32));
	CHECK(a.used == 240 && aggregate.used == 240 && rejected.bytes == 0);
	CHECK(!fx_scene_reserve(&first, &b, &aggregate, 1));
	CHECK(fx_scene_reserve(&second, &b, &aggregate, 256));
	CHECK(!fx_scene_reserve(&rejected, &a, &aggregate, 8));
	CHECK(a.used == 240 && b.used == 256 && aggregate.used == 496);
	fx_scene_release(&first);
	CHECK(a.used == 0 && b.used == 256 && aggregate.used == 256);
	fx_scene_release(&first);
	fx_scene_release(&second);
	CHECK(aggregate.used == 0 && b.used == 0);
	return true;
}

static bool test_mesh(void) {
	struct fx_scene_mesh mesh = {0};
	CHECK(!fx_scene_mesh_create(&mesh, 0, 1, 1));
	CHECK(!fx_scene_mesh_create(&mesh, 65, 1, 1));
	CHECK(!fx_scene_mesh_create(&mesh, 64, 64, 32));
	CHECK(mesh.uv == NULL && mesh.indices == NULL);
	CHECK(fx_scene_mesh_create(&mesh, 64, 64, 31));
	CHECK(mesh.vertices == 4225 && mesh.index_count == 24576);
	double area = 0;
	for (uint32_t i = 0; i < mesh.index_count; i += 3) {
		for (unsigned j = 0; j < 3; j++) {
			CHECK(mesh.indices[i + j] < mesh.vertices);
		}
		float *a = mesh.uv + mesh.indices[i] * 2;
		float *b = mesh.uv + mesh.indices[i + 1] * 2;
		float *c = mesh.uv + mesh.indices[i + 2] * 2;
		double triangle = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
		CHECK(triangle < 0);
		area -= triangle / 2;
	}
	CHECK(fabs(area - 1) < 1e-9);
	CHECK(mesh.uv[0] == 0 && mesh.uv[1] == 0);
	CHECK(mesh.uv[(mesh.vertices - 1) * 2] == 1 && mesh.uv[(mesh.vertices - 1) * 2 + 1] == 1);
	fx_scene_mesh_finish(&mesh);
	fx_scene_mesh_finish(&mesh);
	CHECK(fx_scene_mesh_create(&mesh, 1, 1, 128));
	CHECK(mesh.vertices == 4 && mesh.index_count == 6);
	fx_scene_mesh_finish(&mesh);
	return true;
}

static bool test_framing(void) {
	struct fx_scene_box viewport = {100, 50, 1920, 1080}, extent;
	struct fx_scene_box windows[] = {{-4000, 100, 1000, 900}, {5000, -100, 3000, 2000},
		{500, 100, 500, 500}, {600, 150, 500, 500}};
	const size_t counts[] = {1, 4, 2, 0};
	for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
		CHECK(fx_scene_frame_extent(&viewport, windows, counts[c], false, &extent));
		CHECK(memcmp(&extent, &viewport, sizeof(extent)) == 0);
		CHECK(fx_scene_frame_extent(&viewport, windows, counts[c], true, &extent));
		CHECK(fabs(extent.width / extent.height - 1920.0 / 1080.0) < 1e-9);
		CHECK(extent.x <= viewport.x && extent.y <= viewport.y);
		CHECK(extent.x + extent.width >= viewport.x + viewport.width);
		CHECK(extent.y + extent.height >= viewport.y + viewport.height);
		for (size_t i = 0; i < counts[c]; i++) {
			CHECK(extent.x <= windows[i].x && extent.y <= windows[i].y);
			CHECK(extent.x + extent.width >= windows[i].x + windows[i].width);
			CHECK(extent.y + extent.height >= windows[i].y + windows[i].height);
		}
	}
	windows[0].x = NAN;
	CHECK(!fx_scene_frame_extent(&viewport, windows, 1, true, &extent));
	CHECK(!fx_scene_frame_extent(&viewport, NULL, 1, true, &extent));
	viewport.width = 0;
	CHECK(!fx_scene_frame_extent(&viewport, NULL, 0, false, &extent));
	return true;
}

int main(void) {
	return test_resources() && test_reservations() && test_mesh() && test_framing() ? 0 : 1;
}
