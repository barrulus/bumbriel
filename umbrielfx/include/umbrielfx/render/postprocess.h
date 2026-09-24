#ifndef UMBRIELFX_POSTPROCESS_H
#define UMBRIELFX_POSTPROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <wlr/util/box.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wlr_renderer;
struct fx_postprocess_chain;
struct wlr_scene;
struct wlr_scene_rect;
struct wlr_scene_output;

#define FX_POSTPROCESS_MAX_PASSES 16
// Bounded so the palette stays a fixed-size uniform array in GLSL ES 1.00.
#define FX_PALETTE_MAX 8

enum fx_postprocess_redraw {
  FX_POSTPROCESS_AUTO,
  FX_POSTPROCESS_ON_DAMAGE,
  FX_POSTPROCESS_CONTINUOUS,
};
struct fx_scene_postprocess {
  struct fx_postprocess_chain* chain;
  // Output-local logical box; empty means the whole output.
  struct wlr_box region;
  float cursor_radius;
  // Stored by value so a copied effect carries its own colours; count 0 leaves shaders on theirs.
  float palette[FX_PALETTE_MAX * 4];
  int palette_count;
};

// Source defines vec4 postprocess(vec3), optionally vec4 postprocess_buffer(vec3) for feedback.
struct fx_postprocess_source {
  const char* code;
  const char* label;
  bool buffer;
};

struct fx_postprocess_chain*
fx_postprocess_chain_create(struct wlr_renderer* renderer, const struct fx_postprocess_source* sources, size_t count);
struct fx_postprocess_chain* fx_postprocess_chain_ref(struct fx_postprocess_chain* chain);
void fx_postprocess_chain_unref(struct fx_postprocess_chain* chain);
bool fx_postprocess_chain_animated(const struct fx_postprocess_chain* chain);
bool fx_postprocess_chain_reads_pointer(const struct fx_postprocess_chain* chain);

// Filters the already composited rectangle under the marker; never accepts input.
void wlr_scene_rect_set_postprocess(struct wlr_scene_rect* rect, struct fx_postprocess_chain* chain);
/**
 * The ordered colors this effect's shader reads through umbriel_palette_at().
 * Colors are straight, not premultiplied. A count of zero leaves shaders on
 * their own colours. Counts above FX_PALETTE_MAX are truncated. Set after the
 * chain, which clears any previous palette.
 */
void wlr_scene_rect_set_palette(struct wlr_scene_rect* rect, const float* colors, int count);
// Regions in order, then the global preset, whose chain may be NULL.
void wlr_scene_output_set_postprocess(
    struct wlr_scene_output* output, const struct fx_scene_postprocess* effects, size_t count,
    struct fx_scene_postprocess global, bool in_capture, bool reads_cursor, enum fx_postprocess_redraw redraw
);
bool wlr_scene_output_tick_postprocess(struct wlr_scene_output* output, double seconds, bool suspended);
void wlr_scene_postprocess_pointer(struct wlr_scene* scene, double x, double y, bool visible);

#ifdef __cplusplus
}
#endif

#endif
