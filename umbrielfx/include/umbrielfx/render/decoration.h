#ifndef UMBRIELFX_DECORATION_H
#define UMBRIELFX_DECORATION_H

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct wlr_renderer;
struct wlr_scene_border;
struct wlr_scene_output;
struct wlr_scene;
struct wlr_scene_tree;
struct fx_decoration_shader;

struct fx_decoration_light_parameters {
  bool enabled;
  float spread, intensity, threshold;
};

// Persistent decorations do not occupy animation slots. GLSL supplies
// vec4 ring_color(vec2 coords), returning straight RGBA in logical coordinates.
struct fx_decoration_shader*
fx_decoration_shader_create(struct wlr_renderer* renderer, const char* source, const char* label);
struct fx_decoration_shader* fx_decoration_shader_ref(struct fx_decoration_shader* shader);
void fx_decoration_shader_unref(struct fx_decoration_shader* shader);

struct fx_decoration_parameters {
  float speed;
  float padding;
  // Presentation scale for overview cards; zero defaults to 1.
  float coordinate_scale;
  bool animated;
  struct fx_decoration_light_parameters light;
};

// Spill lives in this separate scene layer, never inside window captures or
// closing snapshots. The layer must belong to scene; NULL removes illumination.
void wlr_scene_set_decoration_light_layer(struct wlr_scene* scene, struct wlr_scene_tree* layer);

// NULL restores the ordinary border. The node owns a program reference.
void wlr_scene_border_set_shader(
    struct wlr_scene_border* border, struct fx_decoration_shader* shader,
    const struct fx_decoration_parameters* parameters
);
void wlr_scene_border_copy_shader(struct wlr_scene_border* destination, struct wlr_scene_border* source);
// Update visible animated decorations and damage this output only. Returns
// whether another shader frame is needed. Frozen/captured rings do not tick.
bool wlr_scene_output_tick_decoration_shaders(struct wlr_scene_output* output, double seconds);

#ifdef __cplusplus
}
#endif
#endif
