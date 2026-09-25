#ifndef FX_EFFECT_PRIVATE_H
#define FX_EFFECT_PRIVATE_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <umbrielfx/render/effect.h>
#include <wayland-server-core.h>

struct fx_renderer;

#define FX_EFFECT_UNIFORM_CACHE 48

struct fx_effect_uniform {
  char name[FX_UNIFORM_NAME_MAX];
  GLint location;
  GLenum type;
  GLint size;
  bool warned;
};

struct fx_effect_shader {
  struct fx_renderer* renderer;
  unsigned references;
  struct wl_listener destroy;
  enum fx_effect_kind kind;
  GLuint program;
  GLint proj, tex_proj, position, tex, sample_matrix;
  GLint previous_tex, previous_sample_matrix;
  GLint progress, linear_progress, direction, random_seed;
  GLint size, scale, expand;
  bool shape_preserving;
  unsigned uniform_count;
  struct fx_effect_uniform uniforms[FX_EFFECT_UNIFORM_CACHE];
};

const struct fx_effect_uniform* fx_effect_shader_uniform(const struct fx_effect_shader* shader, const char* name);
// The program must be in use. A name the program lacks is ignored; a type or
// size mismatch is logged once per program and name, then ignored.
void fx_effect_shader_bind_uniform(struct fx_effect_shader* shader, const struct fx_uniform* uniform);
void fx_effect_shader_bind_parameters(
    struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters
);

#endif
