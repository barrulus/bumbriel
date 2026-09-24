#ifndef UMBRIELFX_WOBBLE_H
#define UMBRIELFX_WOBBLE_H

#include <stdbool.h>
#include <stdint.h>

#define FX_WOBBLE_POINTS 16

// Logical-pixel displacements of a 4x4 elastic sheet. No compositor or GL state.
struct fx_wobble {
  float displacement[FX_WOBBLE_POINTS][2];
  float velocity[FX_WOBBLE_POINTS][2];
  float weights[FX_WOBBLE_POINTS];
  float drag[FX_WOBBLE_POINTS];
  float width, height;
  double remainder;
  uint64_t transition_id;
  bool grabbed, active;
};

void fx_wobble_begin(struct fx_wobble* wobble, float width, float height, float grab_x, float grab_y);
void fx_wobble_move(struct fx_wobble* wobble, float dx, float dy);
void fx_wobble_release(struct fx_wobble* wobble);
// Fixed 240Hz integration. A pause longer than 250ms settles immediately.
bool fx_wobble_tick(struct fx_wobble* wobble, double seconds);

#endif
