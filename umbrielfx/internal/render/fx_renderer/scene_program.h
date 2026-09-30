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
struct fx_scene_picker;
struct fx_effect_light;

enum fx_scene_profile {
	FX_SCENE_PAIR,
	FX_SCENE_SET,
	FX_SCENE_WINDOWS,
};

enum fx_scene_item_kind {
	FX_SCENE_FACE,
	FX_SCENE_CONTENT,
	FX_SCENE_SHADOW,
	FX_SCENE_BORDER,
	FX_SCENE_EMISSION,
	FX_SCENE_STATIC,
};

#define FX_SCENE_PARAMETERS 32
#define FX_SCENE_VERTEX_VECTORS 43u
#define FX_SCENE_FRAGMENT_VECTORS 54u
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
	float navigation_position, navigation_velocity;
	float pointer[2]; // workspace_set: output-local normalized position, top-left origin
	float zoom; // workspace_set: interactive magnification, 1 at entry
	float axis[2], viewport[4];
	int framing; // 0 viewport, 1 fit_all; capture extent belongs to each item
	int scene_count, role;
	int target_token; // 0 for no triggering window; owner tokens are nonzero
	float palette[16];
	int palette_count;
	float audio_levels[4], audio_bands[16];
};

struct fx_scene_item {
	int kind, ordinal, token;
	float current_box[4], source_box[4], destination_box[4];
	float capture_extent[4], content_bounds[4];
	// Source logical layout -> face-local logical coordinates: p*xy+zw.
	float framing_transform[4];
	float motion_progress, linear_motion_progress;
	// Metadata about the captured native presentation. Input pixels already
	// contain native opacity; the wrapper does not multiply it a second time.
	float native_opacity, coverage_box[4];
};

struct fx_scene_sources {
	const char *common, *vertex, *fragment, *composite, *backdrop;
};

// The registry is the only compilation/cache owner. These functions perform
// no file access, selection, subscription, or render-time compilation.
struct fx_scene_program *fx_scene_program_create(struct wlr_renderer *renderer,
	enum fx_scene_profile profile, const struct fx_scene_sources *sources,
	const struct fx_scene_parameter *parameters, unsigned parameter_count);
struct fx_scene_program *fx_scene_program_ref(struct fx_scene_program *program);
void fx_scene_program_unref(struct fx_scene_program *program);
bool fx_scene_program_reads_audio(const struct fx_scene_program *program);
bool fx_scene_program_reads_time(const struct fx_scene_program *program);
bool fx_scene_program_reads_pointer(const struct fx_scene_program *program);
bool fx_scene_program_reads_zoom(const struct fx_scene_program *program);

// Registry-precompiled picking uses the authored vertex and fragment stages.
// Final composites have no inverse mapping contract and are unsupported.
bool fx_scene_program_supports_picking(const struct fx_scene_program *program);
// Fixed CPU metadata (128 bytes) plus one RGBA8 pixel and depth16,
// conservatively accounting four depth bytes. No per-click allocation.
#define FX_SCENE_PICKER_BYTES 136u
// Conservative retained import wrapper metadata, excluding driver-private
// allocations. The underlying source image is already charged by its owner.
#define FX_SCENE_INPUT_METADATA_BYTES 4096u
struct fx_scene_picker *fx_scene_picker_create(struct wlr_renderer *renderer);
void fx_scene_picker_destroy(struct fx_scene_picker *picker);
enum fx_scene_pick_status {
	FX_SCENE_PICK_UNSUPPORTED,
	FX_SCENE_PICK_MISS,
	FX_SCENE_PICK_HIT,
};
struct fx_scene_pick {
	int ordinal, token;
	float uv[2]; // Perspective-correct sampled source UV, quantized to 16 bits.
};

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
// Serial draws may reuse one prepared scratch set across role/version targets.
// Refcounted ownership survives either target being destroyed first.
bool fx_scene_target_share_scratch(struct fx_scene_target *target, struct fx_scene_target *prepared_owner);
// Three reusable padded RGBA targets, independent of participant count. Query
// and reserve before prepare; prepare performs all storage/kernel allocation.
uint64_t fx_scene_target_shadow_bytes(const struct fx_scene_target *target, unsigned padding);
bool fx_scene_target_prepare_shadow(struct fx_scene_target *target, unsigned padding);

// Includes the shared padded scratch once plus all fixed recipe pyramids.
// Conservatively budgets FP16 caches even when the renderer selects RGBA8.
uint64_t fx_scene_target_light_bytes(const struct fx_scene_target *target, unsigned padding,
	const struct fx_effect_light *recipes, unsigned count, float scale);
bool fx_scene_target_prepare_light(struct fx_scene_target *target, unsigned padding,
	const struct fx_effect_light *recipes, unsigned count, float scale);

struct fx_scene_shadow {
	float softness, offset[2], color[4]; // Physical pixels; native straight sRGB color.
	// Optional retained native analytic companion in physical output coordinates.
	// Generic lifecycle policy converges to it with an additive premultiplied mix.
	struct wlr_texture *native_texture;
	int native_box[4];
	float native_mix;
};

struct fx_scene_input {
	struct wlr_texture *texture;
	// Column-major GL mat3 mapping logical top-left UV to imported texture UV.
	// NULL means identity.
	const float *sample_matrix;
};

// Opaque native geometry coverage for shadow input, independent of client alpha.
// box/corners are logical and local to frame.output_size; corners TL,TR,BR,BL.
bool fx_scene_target_mask(struct fx_scene_target *target, const struct fx_scene_frame *frame,
	const float box[4], const float corners[4]);

// Same-oriented physical canvases, resized to target. Identity sampling only;
// weight is the second source's contribution. Uses existing renderer programs.
bool fx_scene_target_blend(struct fx_scene_target *target,
	const struct fx_scene_input *first, const struct fx_scene_input *second,
	float weight, bool working_space);

struct fx_scene_draw {
	struct fx_scene_item item;
	struct fx_scene_input input;
	const struct fx_scene_mesh *mesh;
	bool emission;
	// Input is the owner's opaque geometry coverage, not client color/alpha.
	// The authored stages deform/mask it before the native shadow kernel.
	const struct fx_scene_shadow *shadow;
	// FX_SCENE_EMISSION: input is retained raw native emission. Deform it once,
	// then apply this preflighted native threshold/pyramid/screen blend recipe.
	const struct fx_effect_light *light;
};

// Render a complete isolated composition. Targets cannot alias inputs or one
// another. If the bundle has a composite stage, composed is a separate prepared
// target for the first stage. The caller commits/suppresses native only after
// this succeeds. Failure leaves target contents unspecified.
bool fx_scene_program_render(struct fx_scene_program *program,
	struct fx_scene_target *target, struct fx_scene_target *composed,
	const struct fx_scene_frame *frame, const struct fx_scene_input pair[2],
	const struct fx_scene_draw *draws, unsigned draw_count);

// Supply retained state from the last successful output submission. x/y are
// logical output coordinates. Does not advance time/audio, allocate, or change
// target pixels. Only a single source sample has an unambiguous window mapping;
// multiple/no samples fail closed, while fragment discard preserves visibility.
enum fx_scene_pick_status fx_scene_program_pick(struct fx_scene_program *program,
	struct fx_scene_picker *picker, struct fx_scene_target *target,
	const struct fx_scene_frame *frame, const struct fx_scene_draw *draws,
	unsigned draw_count, float x, float y, struct fx_scene_pick *result);

#endif
