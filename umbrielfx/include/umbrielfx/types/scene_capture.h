#ifndef UMBRIELFX_TYPES_SCENE_CAPTURE_H
#define UMBRIELFX_TYPES_SCENE_CAPTURE_H

#include <wlr/types/wlr_ext_image_capture_source_v1.h>

struct wlr_scene_node;
struct wlr_scene_buffer;
struct wlr_scene_output;

/**
 * Destroy an owned scene capture source, stopping all sessions and making old
 * source resources inert. The source scene and its nodes remain alive.
 * Call outside the source's render/copy callbacks. NULL is accepted.
 */
void umbrielfx_scene_capture_source_destroy(struct wlr_ext_image_capture_source_v1* source);

/** Harness prototype: exclude a node's descendants from an independent capture composition. */
bool umbrielfx_scene_node_set_capture_excluded_for_test(struct wlr_scene_node* node, bool excluded);
bool umbrielfx_scene_node_capture_excluded_for_test(struct wlr_scene_node* node);
bool umbrielfx_scene_buffer_captured_for_test(struct wlr_scene_buffer* buffer, struct wlr_scene_output* output);

/**
 * Set the render scale of a source created with
 * wlr_ext_image_capture_source_v1_create_with_scene_node().
 * The scale must be finite and positive. Active sessions receive updated buffer
 * constraints and a new frame; idle sources do not render until started.
 */
void umbrielfx_scene_capture_source_set_scale(struct wlr_ext_image_capture_source_v1* source, float scale);

#endif
