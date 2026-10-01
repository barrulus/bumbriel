#ifndef TYPES_WLR_SCENE_H
#define TYPES_WLR_SCENE_H

#include <wlr/types/wlr_scene.h>

struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node);

void scene_node_get_size(struct wlr_scene_node *node, int *width, int *height);

void scene_surface_set_clip(struct wlr_scene_surface *surface, struct wlr_box *clip);

void scene_surface_set_scale(struct wlr_scene_surface *surface, double scale);

void scene_subsurface_tree_set_point_accepts_input(struct wlr_scene_tree *tree,
		wlr_scene_buffer_point_accepts_input_func_t point_accepts_input);
struct fx_effect_shader;
struct fx_animation_parameters;
// Synchronous composition-only uniform rebind. Program and every non-uniform
// field must match the installed slot; otherwise returns false for the normal
// setter. damage marks only this output without scheduling a new frame; the
// caller sets it only when this output has pending input. Geometry/history stay intact.
bool wlr_scene_node_set_animation_uniforms_for_output(struct wlr_scene_node *node,
	unsigned slot, struct fx_effect_shader *shader,
	const struct fx_animation_parameters *parameters, struct wlr_scene_output *output, bool damage);

// C0 experiment, not a public capture API. Capture an inclusive range of root
// siblings using source-local coverage (including pixels hidden by excluded
// overlay nodes). The caller supplies the full output-sized target. Output
// effects, software cursors and output_sample callbacks are excluded.
// Current experiment is SDR and does not establish frozen history ownership.
bool fx_scene_capture_range_for_test(struct wlr_scene_output *output,
	struct wlr_scene_node *first, struct wlr_scene_node *last,
	struct wlr_buffer *target, bool unfiltered);

// Experimental persistent source owner. Acquisition snapshots both native role
// histories atomically; later source frames cannot observe native promotion.
// begin_frame drops cached role images. finish_frame promotes only after the
// caller's final output submission succeeded; failed frames retain old history.
struct fx_scene_source_session;
uint64_t fx_scene_source_session_bytes_for_test(struct wlr_scene_output *output,
	struct wlr_scene_node *first, struct wlr_scene_node *last);
struct fx_scene_source_session *fx_scene_source_session_create_for_test(
	struct wlr_scene_output *output, struct wlr_scene_node *first,
	struct wlr_scene_node *last, uint64_t reserved_bytes);
bool fx_scene_source_session_begin_frame_for_test(struct fx_scene_source_session *session);
bool fx_scene_source_session_capture_for_test(struct fx_scene_source_session *session,
	struct wlr_buffer *target, bool unfiltered);
void fx_scene_source_session_finish_frame_for_test(struct fx_scene_source_session *session, bool submitted);
void fx_scene_source_session_destroy_for_test(struct fx_scene_source_session *session);

enum fx_scene_participant_reason {
	FX_SCENE_PARTICIPANT_SUPPORTED,
	FX_SCENE_PARTICIPANT_IN_PLACE,
	FX_SCENE_PARTICIPANT_BLUR,
	FX_SCENE_PARTICIPANT_TOPOLOGY,
};

// C0 rigid-participant extraction only. Companion sources (shadow and light)
// remain separate ordered items; no grid or fold compatibility is implied.
enum fx_scene_participant_reason fx_scene_participant_admit_for_test(struct wlr_scene_node *node);
bool fx_scene_capture_participant_for_test(struct wlr_scene_output *output,
	struct wlr_scene_node *node, struct wlr_buffer *target);
// Expanded logical layout extent retains off-output companion pixels which
// deformation may bring back into view. Target dimensions must match scale.
bool fx_scene_capture_participant_extent_for_test(struct wlr_scene_output *output,
	struct wlr_scene_node *node, struct wlr_buffer *target, const struct wlr_box *extent);

struct fx_scene_source_pair_for_test {
	struct wlr_buffer *display;
	struct wlr_buffer *unfiltered;
	uint64_t reserved_bytes;
	// FP16 linear source; landing must declare EXT_LINEAR before final encoding.
	bool working_space;
	bool floating_point;
};

// Conservative C0 peak budget, including both frozen roles and full-sized
// scratch up to the legacy capture-depth bound. The lease owner reserves this
// against output and aggregate pools before calling capture, and retains the
// reservation until finish. 0 means unsupported dimensions/budget.
uint64_t fx_scene_source_pair_bytes_for_test(struct wlr_scene_output *output);
// Exact native range freeze: feature-sensitive scratch and copied committed
// histories only (no writable history images for a frozen frame).
uint64_t fx_scene_source_frozen_pair_bytes(struct wlr_scene_output *output,
    struct wlr_scene_node *first, struct wlr_scene_node *last);
bool fx_scene_source_pair_capture_for_test(struct wlr_scene_output *output,
	struct wlr_scene_node *first, struct wlr_scene_node *last,
	uint64_t reserved_bytes, struct fx_scene_source_pair_for_test *pair);
void fx_scene_source_pair_finish_for_test(struct fx_scene_source_pair_for_test *pair);

// Internal G4 probe only. Replaces ordinary render entries of an inclusive
// root range on this output without changing scene enabled/geometry/membership.
// Caller installs an opaque presentation backing above the range. NULL/NULL
// clears. Source captures are independent and ignore this replacement.
bool fx_scene_output_replace_range_for_test(struct wlr_scene_output *output,
	struct wlr_scene_node *first, struct wlr_scene_node *last);

// The replacement's picture uses its ordinary buffer for display and this
// retained buffer for the output's unfiltered capture composition. Dimensions
// and colorimetry must match; one buffer may alias both roles. Clear/destroy of
// the replacement releases the lock. Picture destruction cancels replacement.
bool fx_scene_output_bind_replacement_roles_for_test(struct wlr_scene_output *output,
	struct wlr_scene_buffer *picture, struct wlr_buffer *unfiltered);

#endif
