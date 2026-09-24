#ifndef FX_DECORATION_PRIVATE_H
#define FX_DECORATION_PRIVATE_H
#include <GLES2/gl2.h>
#include <umbrielfx/render/decoration.h>
#include <wayland-server-core.h>

struct fx_decoration_shader {
  struct fx_uniform_values* params;
  struct fx_renderer* renderer;
  unsigned references;
  struct wl_listener destroy;
  GLuint program;
  GLint proj, tex_proj, position;
  GLint size, raster, origin, radius, width, padding, time, scale, color, linear;
  GLint emission, threshold, emission_bounds, palette, palette_count;
  GLuint light_program;
  GLint light_proj, light_tex_proj, light_position, light_tex, light_gain, light_linear;
  GLint light_emission, light_source_linear, light_threshold, light_source_region;
};

struct fx_decoration_light;
struct fx_decoration_pipeline;
void fx_decoration_pipeline_destroy(struct fx_decoration_pipeline* pipeline);
struct fx_gles_render_pass;
struct fx_render_border_options;
struct wlr_box;
typedef struct pixman_region32 pixman_region32_t;
void fx_decoration_light_destroy(struct fx_decoration_light* light);
bool fx_render_pass_add_decoration_light(
    struct fx_gles_render_pass* pass, struct fx_decoration_light** cache, const struct fx_render_border_options* ring,
    const struct fx_decoration_light_parameters* parameters, const struct wlr_box* box, const pixman_region32_t* clip
);
#endif
