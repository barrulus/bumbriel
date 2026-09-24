#ifndef FX_POSTPROCESS_PRIVATE_H
#define FX_POSTPROCESS_PRIVATE_H

#include <pixman.h>
#include <umbrielfx/render/postprocess.h>
#include <umbrielfx/types/fx/clipped_region.h>
#include "types/fx/clipped_region.h"
#include <wayland-server-core.h>

struct fx_gles_render_pass;
struct fx_postprocess_state;
struct fx_postprocess_parameters {
  float time, scale;
  float cursor[2], output_size[2], region[4];
  const float* palette;
  int palette_count;
  // Buffer-space box; GLSL runs in logical orientation with output-normalised region UVs.
  struct wlr_box box;
  enum wl_output_transform transform;
  struct fx_corner_radii corners;
  struct clipped_fregion hole;
};

// advance=false reads history without promoting it; promotion waits for a successful submit.
bool fx_render_pass_postprocess(
    struct fx_gles_render_pass* pass, struct fx_postprocess_state** state, struct fx_postprocess_chain* chain,
    const struct fx_postprocess_parameters* parameters, const pixman_region32_t* clip, bool advance
);
void fx_postprocess_state_destroy(struct fx_postprocess_state* state);
void fx_postprocess_commit(struct wl_list* updates, bool success);

#endif
