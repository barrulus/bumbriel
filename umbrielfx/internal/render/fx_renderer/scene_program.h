#ifndef FX_SCENE_PROGRAM_PRIVATE_H
#define FX_SCENE_PROGRAM_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>
#include "scene_resources.h"

struct wlr_renderer;
struct wlr_allocator;
struct wlr_texture;
struct wlr_buffer;
struct fx_scene_program;
struct fx_scene_target;
struct fx_effect_light;

enum fx_scene_profile {
	FX_SCENE_PAIR,
};

#define FX_SCENE_PARAMETERS 32
#define FX_SCENE_VERTEX_VECTORS 41u
#define FX_SCENE_FRAGMENT_VECTORS 52u
struct fx_scene_limits {
	unsigned texture_size, vertex_vectors, fragment_vectors, fragment_texture_units;
};
bool fx_scene_program_get_limits(struct wlr_renderer *renderer, struct fx_scene_limits *limits);
struct fx_scene_parameter {
	char name[32];
	unsigned components;
	float value[4];
};

// Experimental scene-only descriptors. Never embedded in legacy slot storage.
struct fx_scene_frame {
	float output_size[2], scale, time;
	unsigned output_transform; // wl_output_transform, logical output to physical target
	float progress, linear_progress, direction, random_seed[4];
	float axis[2], viewport[4];
	int scene_count, role;
	float palette[16];
	int palette_count;
};

struct fx_scene_sources {
	const char *common, *fragment;
};

// The registry is the only compilation/cache owner. These functions perform
// no file access, selection, subscription, or render-time compilation.
struct fx_scene_program *fx_scene_program_create(struct wlr_renderer *renderer,
	enum fx_scene_profile profile, const struct fx_scene_sources *sources,
	const struct fx_scene_parameter *parameters, unsigned parameter_count);
struct fx_scene_program *fx_scene_program_ref(struct fx_scene_program *program);
void fx_scene_program_unref(struct fx_scene_program *program);
bool fx_scene_program_reads_time(const struct fx_scene_program *program);

// Caller owns one buffer reference; returns NULL for unsupported formats/size.
struct wlr_buffer *fx_scene_buffer_create(struct wlr_renderer *renderer,
	struct wlr_allocator *allocator, int width, int height, bool floating_point);

// Prepared before acquiring presentation. Retains the supplied buffer and
// prepares depth storage for scene-set targets. Caller budgets both resources.
struct fx_scene_target *fx_scene_target_create(struct wlr_renderer *renderer,
	struct wlr_buffer *buffer, bool depth);
// Explicit value encoding, including gamma-valued FP16 for unmanaged 10-bit.
struct fx_scene_target *fx_scene_target_create_with_color(struct wlr_renderer *renderer,
	struct wlr_buffer *buffer, bool depth, bool working_space);
void fx_scene_target_destroy(struct fx_scene_target *target);
struct fx_scene_input {
	struct wlr_texture *texture;
	// Column-major GL mat3 mapping logical top-left UV to imported texture UV.
	// NULL means identity.
	const float *sample_matrix;
};

// Render the isolated pair. Targets cannot alias inputs. The caller suppresses
// native rendering only after success. Failure leaves target contents unspecified.
bool fx_scene_program_render(struct fx_scene_program *program,
	struct fx_scene_target *target, const struct fx_scene_frame *frame, const struct fx_scene_input pair[2]);

#endif
