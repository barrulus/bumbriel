#ifndef FX_POSTPROCESS_PRIVATE_H
#define FX_POSTPROCESS_PRIVATE_H

#include <pixman.h>
#include <umbrielfx/render/postprocess.h>
#include <umbrielfx/types/fx/clipped_region.h>
#include <wayland-server-core.h>

struct fx_gles_render_pass;
struct fx_postprocess_state;
struct fx_postprocess_parameters {
  float time, scale;
  float cursor[2], output_size[2], region[4];
  // Source rectangle in buffer coordinates. Logical orientation is restored
  // before executing GLSL. Region UVs remain output-normalized.
  struct wlr_box box;
  enum wl_output_transform transform;
  struct fx_corner_radii corners;
};

// State belongs to one chain instance on one output. Passing advance=false is
// suitable for a capture: history is read but never promoted. On true, promotion
// is deferred until successful render-pass submission.
bool fx_render_pass_postprocess(
    struct fx_gles_render_pass* pass, struct fx_postprocess_state** state, struct fx_postprocess_chain* chain,
    const struct fx_postprocess_parameters* parameters, const pixman_region32_t* clip, bool advance
);
void fx_postprocess_state_destroy(struct fx_postprocess_state* state);
void fx_postprocess_commit(struct wl_list* updates, bool success);

#endif
