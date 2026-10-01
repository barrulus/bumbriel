#ifndef UMBRIELFX_INTERNAL_SCENE_SOURCE_H
#define UMBRIELFX_INTERNAL_SCENE_SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <umbrielfx/render/effect.h>
#include <wlr/types/wlr_scene.h>

// Private C0 view contract; no author-facing ABI or configuration surface.
// No operation mutates native node enabled/geometry/clips/output membership.
#define FX_SCENE_SOURCE_ROOT_LIMIT 1024
#define FX_SCENE_SOURCE_CLIP_LIMIT 64

enum fx_scene_source_visibility {
  FX_SCENE_SOURCE_INHERIT,
  FX_SCENE_SOURCE_VISIBLE,
  FX_SCENE_SOURCE_HIDDEN,
};

enum fx_scene_source_framing {
  FX_SCENE_SOURCE_CONTENT,
  FX_SCENE_SOURCE_VIEWPORT,
};

struct fx_scene_source_root_override {
  struct wlr_scene_node *root;
  enum fx_scene_source_visibility visibility;
  // Additive logical translation of this root and descendants. Nested
  // overrides compose. Every sum is checked for integer overflow.
  int offset_x, offset_y;
  // Only top-level strata may select viewport framing. Nested roots inherit
  // their stratum; visibility and translations still apply normally.
  enum fx_scene_source_framing framing;
};

struct fx_scene_source_node_override {
  struct wlr_scene_node *node;
  uint32_t skip_slots; // Exact replaced stages; every other installed stage remains.
  bool skip_animation_clip;
  bool has_opacity;
  float opacity; // Absolute scene-buffer opacity, including unrelated/client factors.
  bool has_colors;
  float colors[2][4]; // Rect/shadow color[0], border inner[0]/outer[1].
};

struct fx_scene_source_session;

struct fx_scene_source_view {
  struct wlr_scene_node *first, *last;
  const struct fx_scene_source_root_override *roots;
  size_t root_count;
  // Nominated scroll/output viewport clips only. Window/content clips remain.
  struct wlr_scene_tree *const *bypass_clips;
  size_t bypass_clip_count;
  // Bounded face canvas, in layout coordinates. May be viewport or fit_all.
  struct wlr_box extent;
  float scale;
  // Shared output-attached strata retain this normal viewport while content
  // strata use extent/scale. Empty unless a root selects VIEWPORT framing.
  struct wlr_box viewport;
  // Participant capture may select nested same-parent roots, with transparent
  // outside coverage. Overrides never write native slots or visual properties.
  bool transparent;
  const struct fx_scene_source_node_override *nodes;
  size_t node_count;
  // Internal raw-emission acquisition; ordinary workspace views leave NULL.
  struct wlr_scene_node *emission_owner;
  // One live occurrence owns independent role histories; never shared by faces.
  struct fx_scene_source_session *session;
};

// Retained raw border emission. Both roles are captured from completed native
// emissions, never by evaluating a feedback stage again. Metadata is in source
// layout coordinates; pixels use the producing output's physical orientation.
struct fx_scene_emission_source {
  struct wlr_buffer *display, *unfiltered;
  struct wlr_box extent;
  struct fx_effect_light recipe;
  uint64_t reserved_bytes;
  bool working_space; // Linear values; independent of storage precision.
  bool floating_point;
};
uint64_t fx_scene_emission_source_bytes(struct wlr_scene_output *output, struct wlr_scene_node *owner);
bool fx_scene_emission_source_capture(struct wlr_scene_output *output, struct wlr_scene_node *owner,
    uint64_t reserved_bytes, struct fx_scene_emission_source *source);
void fx_scene_emission_source_finish(struct fx_scene_emission_source *source);
// Nonfeedback cold source: use source-only target overrides, synthesize raw
// emission even before a native proxy exists. No native cache/history writes.
struct fx_scene_emission_view_plan {
  uint64_t retained_bytes, capture_bytes, total_bytes;
};
bool fx_scene_emission_view_plan(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, struct wlr_scene_node *owner,
    struct fx_scene_emission_view_plan *plan);
uint64_t fx_scene_emission_view_bytes(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, struct wlr_scene_node *owner);
bool fx_scene_emission_view_capture(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, struct wlr_scene_node *owner,
    uint64_t reserved_bytes, struct fx_scene_emission_source *source);

// Native light-stratum discovery. Existing occurrences retain native child
// order; a cold owner would append its proxy at the end of this layer.
struct wlr_scene_tree *fx_scene_source_light_layer(struct wlr_scene *scene);
struct wlr_scene_node *fx_scene_source_light_owner(struct wlr_scene_node *occurrence);

// Read-only source working-space selection, before reserving any role targets.
bool fx_scene_source_working_space(struct wlr_scene_output *output);
bool fx_scene_source_floating_point(struct wlr_scene_output *output);
// Conservative visual bounds in source-layout coordinates for one framing
// class, including selected transient buffers, native shadow and effect halos.
// Explicit bypass clips apply; native enable/geometry/membership is untouched.
bool fx_scene_source_view_bounds_for_test(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, enum fx_scene_source_framing framing,
    struct wlr_box *bounds);

// Validate the complete range and every override before allocation. Duplicate,
// foreign, missing, unbounded, and unsupported roots cause explicit failure.
// Lighting occurrences are synthesized from admitted border stages even when
// no native proxy survives for a hidden owner. The native light stratum and
// screen blending remain authoritative.
// Inventory budgeting: reserve all retained images plus the maximum capture
// peak when sources are captured sequentially. Old images stay charged during
// atomic refresh. Native-resolution landing storage is separate.
struct fx_scene_source_view_plan {
  uint64_t retained_bytes, capture_bytes, total_bytes;
  uint64_t history_bytes; // Persistent session reservation, separate from total_bytes.
  uint64_t scratch_bytes; // Transient GPU images; capture_bytes also includes CPU/import overhead.
  int width, height;
  bool working_space; // Linear values; independent of storage precision.
  bool floating_point;
};
uint64_t fx_scene_source_view_history_bytes(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view);
struct fx_scene_source_session *fx_scene_source_view_session_create(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, uint64_t reserved_history_bytes);
bool fx_scene_source_view_session_matches(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, struct fx_scene_source_session *session);
bool fx_scene_source_view_plan_for_test(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, struct fx_scene_source_view_plan *plan);
// Exact source-layout -> face-local logical transform, (scale.xy, offset.xy).
// Use these values in fx_scene_item.framing_transform; do not round separately.
bool fx_scene_source_view_framing_for_test(const struct fx_scene_source_view *view,
    enum fx_scene_source_framing framing, float transform[4]);
// Capture-time native leaf footprints, in face-local logical coordinates and
// bottom-to-top paint order. The callback must not mutate the scene. NULL counts
// entries for reservation before snapshot allocation. Shadows/lights are not
// interactive; shared strata are included so they can occlude window targets.
typedef bool (*fx_scene_source_hit_iterator)(struct wlr_scene_node *node,
    const struct wlr_fbox *box, void *data);
bool fx_scene_source_view_hits(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, fx_scene_source_hit_iterator iterator,
    void *data, size_t *count);
uint64_t fx_scene_source_view_bytes_for_test(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view);
bool fx_scene_capture_view_for_test(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, struct wlr_buffer *target,
    bool unfiltered, uint64_t reserved_bytes);

// Allocates both complete role images before publishing the result. Uses the
// same peak reservation query; finish with fx_scene_source_pair_finish_for_test.
struct fx_scene_source_pair_for_test;
bool fx_scene_source_view_pair_capture_for_test(struct wlr_scene_output *output,
    const struct fx_scene_source_view *view, uint64_t reserved_bytes,
    struct fx_scene_source_pair_for_test *pair);

#endif
