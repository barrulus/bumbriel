#include "render/fx_renderer/params.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

static unsigned uniform_length(GLenum type) {
  switch (type) {
  case GL_BOOL:
  case GL_INT:
  case GL_FLOAT:
    return 1;
  case GL_INT_VEC2:
  case GL_FLOAT_VEC2:
    return 2;
  case GL_INT_VEC3:
  case GL_FLOAT_VEC3:
    return 3;
  case GL_INT_VEC4:
  case GL_FLOAT_VEC4:
    return 4;
  default:
    return 0;
  }
}

struct fx_uniform_values*
fx_uniform_values_create(GLuint program, const struct fx_shader_param* params, size_t count, bool allow_inactive) {
  GLint active = 0, max_length = 0;
  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active);
  glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_length);
  if ((size_t)active > (SIZE_MAX - sizeof(struct fx_uniform_values)) / sizeof(struct fx_uniform_value)
      || count > (SIZE_MAX - sizeof(struct fx_uniform_values)) / sizeof(struct fx_uniform_value) - (size_t)active
      || (count != 0 && params == NULL))
    return NULL;
  struct fx_uniform_values* values = calloc(1, sizeof(*values) + (count + (size_t)active) * sizeof(values->values[0]));
  if (values == NULL)
    return NULL;
  values->count = count;
  char* name = calloc((size_t)max_length + 1, 1);
  if (name == NULL) {
    free(values);
    return NULL;
  }
  for (size_t i = 0; i < count; ++i) {
    const struct fx_shader_param* param = &params[i];
    struct fx_uniform_value* value = &values->values[i];
    value->location = -1;
    if (param->name == NULL || param->length == 0 || param->length > 4)
      goto fail;
    for (GLint j = 0; j < active; ++j) {
      GLint size = 0;
      GLenum type = 0;
      glGetActiveUniform(program, (GLuint)j, max_length, NULL, &size, &type, name);
      if (strcmp(name, param->name) != 0)
        continue;
      bool integer = type == GL_INT || type == GL_INT_VEC2 || type == GL_INT_VEC3 || type == GL_INT_VEC4;
      if (size != 1
          || uniform_length(type) != param->length
          || (integer && param->type != FX_PARAM_INT)
          || (type == GL_BOOL && (param->type != FX_PARAM_BOOL || param->length != 1))
          || (type != GL_BOOL && param->type == FX_PARAM_BOOL)) {
        wlr_log(WLR_ERROR, "Shader parameter '%s' does not match its linked uniform type", param->name);
        goto fail;
      }
      value->location = glGetUniformLocation(program, param->name);
      value->type = type;
      for (unsigned component = 0; component < param->length; ++component) {
        if (integer || type == GL_BOOL)
          value->value.integers[component] = param->integers[component];
        else {
          value->value.numbers[component] =
              param->type == FX_PARAM_INT ? (float)param->integers[component] : param->numbers[component];
          if (!isfinite(value->value.numbers[component]))
            goto fail;
        }
      }
      break;
    }
    if (value->location < 0 && !allow_inactive) {
      wlr_log(WLR_ERROR, "Shader parameter '%s' is unknown or inactive", param->name);
      goto fail;
    }
  }
  for (GLint j = 0; j < active; ++j) {
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(program, (GLuint)j, max_length, NULL, &size, &type, name);
    if (size != 1 || uniform_length(type) == 0 || strchr(name, '[') != NULL
        || strncmp(name, "umbriel_", 8) == 0 || strncmp(name, "ring_", 5) == 0
        || strncmp(name, "effect_", 7) == 0 || strncmp(name, "gl_", 3) == 0)
      continue;
    const GLint location = glGetUniformLocation(program, name);
    bool supplied = false;
    for (size_t i = 0; i < count; ++i)
      supplied |= values->values[i].location == location;
    if (!supplied)
      values->values[values->count++] = (struct fx_uniform_value){.location = location, .type = type};
  }
  free(name);
  return values;
fail:
  free(name);
  free(values);
  return NULL;
}

void fx_uniform_values_bind(const struct fx_uniform_values* values) {
  if (values == NULL)
    return;
  for (size_t i = 0; i < values->count; ++i) {
    const struct fx_uniform_value* value = &values->values[i];
    if (value->location < 0)
      continue;
    const GLint* integers = value->value.integers;
    const GLfloat* numbers = value->value.numbers;
    switch (value->type) {
    case GL_BOOL:
    case GL_INT:
      glUniform1iv(value->location, 1, integers);
      break;
    case GL_INT_VEC2:
      glUniform2iv(value->location, 1, integers);
      break;
    case GL_INT_VEC3:
      glUniform3iv(value->location, 1, integers);
      break;
    case GL_INT_VEC4:
      glUniform4iv(value->location, 1, integers);
      break;
    case GL_FLOAT:
      glUniform1fv(value->location, 1, numbers);
      break;
    case GL_FLOAT_VEC2:
      glUniform2fv(value->location, 1, numbers);
      break;
    case GL_FLOAT_VEC3:
      glUniform3fv(value->location, 1, numbers);
      break;
    case GL_FLOAT_VEC4:
      glUniform4fv(value->location, 1, numbers);
      break;
    }
  }
}
