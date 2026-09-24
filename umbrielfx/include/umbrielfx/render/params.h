#ifndef UMBRIELFX_PARAMS_H
#define UMBRIELFX_PARAMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum fx_param_type { FX_PARAM_BOOL, FX_PARAM_INT, FX_PARAM_FLOAT };
struct fx_shader_param {
  const char* name;
  enum fx_param_type type;
  unsigned length;
  int32_t integers[4];
  float numbers[4];
};

#endif
