#ifndef UMBRIELFX_DRAG_PHYSICS_H
#define UMBRIELFX_DRAG_PHYSICS_H

#include <stdbool.h>
#include <stdint.h>

#define FX_DRAG_PHYSICS_POINTS 16

struct fx_drag_physics_parameters {
  float stiffness, coupling, damping, pointer_response;
  float stiffness_gradient, lag_gradient;
  float downward_pull, motion_gain, decay;
};

struct fx_drag_physics_parameters fx_drag_physics_default_parameters(void);
bool fx_drag_physics_parameters_valid(const struct fx_drag_physics_parameters* parameters);

// Logical-pixel displacements of a 4x4 elastic sheet. No compositor or GL state.
struct fx_drag_physics {
  float displacement[FX_DRAG_PHYSICS_POINTS][2];
  float velocity[FX_DRAG_PHYSICS_POINTS][2];
  float weights[FX_DRAG_PHYSICS_POINTS];
  float drag[FX_DRAG_PHYSICS_POINTS];
  float width, height;
  float grab_y, stretch;
  struct fx_drag_physics_parameters parameters;
  double remainder;
  uint64_t transition_id;
  bool grabbed, active;
};

void fx_drag_physics_begin(struct fx_drag_physics* physics, float width, float height, float grab_x, float grab_y);
bool fx_drag_physics_set_parameters(struct fx_drag_physics* physics, const struct fx_drag_physics_parameters* parameters);
void fx_drag_physics_move(struct fx_drag_physics* physics, float dx, float dy);
void fx_drag_physics_release(struct fx_drag_physics* physics);
// Fixed 240Hz integration. A pause longer than 250ms settles immediately.
bool fx_drag_physics_tick(struct fx_drag_physics* physics, double seconds);

#endif
