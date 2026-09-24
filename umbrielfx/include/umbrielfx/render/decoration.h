#ifndef UMBRIELFX_DECORATION_H
#define UMBRIELFX_DECORATION_H

#include <stdbool.h>
#include <umbrielfx/render/params.h>
#ifdef __cplusplus
extern "C" {
#endif
struct wlr_renderer;
struct wlr_scene_border;
struct wlr_scene_output;
struct wlr_scene;
struct wlr_scene_tree;
struct fx_decoration_shader;
struct fx_postprocess_chain;

struct fx_decoration_light_parameters {
  bool enabled;
  float spread, intensity, threshold;
};

// Source defines vec4 ring_color(vec2 logical) returning straight RGBA.
struct fx_decoration_shader*
fx_decoration_shader_create(struct wlr_renderer* renderer, const char* source, const char* label);
bool fx_decoration_shader_set_params(
    struct fx_decoration_shader* shader, const struct fx_shader_param* params, size_t count
);
struct fx_decoration_shader* fx_decoration_shader_ref(struct fx_decoration_shader* shader);
void fx_decoration_shader_unref(struct fx_decoration_shader* shader);

struct fx_decoration_parameters {
  float speed;
  float padding;
  float coordinate_scale; // zero means 1
  bool animated;
  struct fx_decoration_light_parameters light;
};

// Layer that receives ring illumination; NULL disables it.
void wlr_scene_set_decoration_light_layer(struct wlr_scene* scene, struct wlr_scene_tree* layer);

// NULL restores the plain border.
void wlr_scene_border_set_shader(
    struct wlr_scene_border* border, struct fx_decoration_shader* shader,
    const struct fx_decoration_parameters* parameters
);
void wlr_scene_border_set_postprocess(struct wlr_scene_border* border, struct fx_postprocess_chain* chain);
void wlr_scene_border_copy_shader(struct wlr_scene_border* destination, struct wlr_scene_border* source);
// Returns whether an animated ring on this output needs another frame.
bool wlr_scene_output_tick_decoration_shaders(struct wlr_scene_output* output, double seconds);

#ifdef __cplusplus
}
#endif
#endif
