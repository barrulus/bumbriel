#ifndef FX_PARAMS_PRIVATE_H
#define FX_PARAMS_PRIVATE_H

#include <GLES2/gl2.h>
#include <umbrielfx/render/params.h>

struct fx_uniform_value {
  GLint location;
  GLenum type;
  union {
    GLint integers[4];
    GLfloat numbers[4];
  } value;
};
struct fx_uniform_values {
  size_t count;
  struct fx_uniform_value values[];
};

struct fx_uniform_values*
fx_uniform_values_create(GLuint program, const struct fx_shader_param* params, size_t count, bool allow_inactive);
void fx_uniform_values_bind(const struct fx_uniform_values* values);

#endif
