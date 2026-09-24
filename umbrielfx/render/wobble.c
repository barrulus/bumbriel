#include <math.h>
#include <string.h>
#include <umbrielfx/render/wobble.h>

static void constrain(struct fx_wobble* w) {
  if (w->grabbed) {
    float sum = 0, position[2] = {0}, velocity[2] = {0};
    for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
      sum += w->weights[i] * w->weights[i];
      for (int axis = 0; axis < 2; axis++) {
        position[axis] += w->weights[i] * w->displacement[i][axis];
        velocity[axis] += w->weights[i] * w->velocity[i][axis];
      }
    }
    // Pin the interpolated grab point exactly, even between grid vertices.
    for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
      for (int axis = 0; axis < 2; axis++) {
        w->displacement[i][axis] -= w->weights[i] * position[axis] / sum;
        w->velocity[i][axis] -= w->weights[i] * velocity[axis] / sum;
      }
    }
  }
  // Bound the deformation gradient so inverse texture mapping cannot fold.
  // A global scale preserves the pin constraint, unlike clamping each mass.
  float ratio = 1;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
    ratio = fmaxf(ratio, fabsf(w->displacement[i][0]) / fminf(80, w->width / 24));
    ratio = fmaxf(ratio, fabsf(w->displacement[i][1]) / fminf(80, w->height / 24));
    for (int axis = 0; axis < 2; axis++)
      ratio = fmaxf(ratio, fabsf(w->velocity[i][axis]) / 4000);
  }
  if (ratio > 1) {
    for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
      for (int axis = 0; axis < 2; axis++) {
        w->displacement[i][axis] /= ratio;
        w->velocity[i][axis] /= ratio;
      }
    }
  }
}

void fx_wobble_begin(struct fx_wobble* w, float width, float height, float grab_x, float grab_y) {
  static uint64_t serial;
  *w = (struct fx_wobble){
      .width = fmaxf(width, 1), .height = fmaxf(height, 1), .transition_id = ++serial, .grabbed = true
  };
  float x = fminf(fmaxf(grab_x, 0), 1) * 3;
  float y = fminf(fmaxf(grab_y, 0), 1) * 3;
  float tx = x - floorf(x), ty = y - floorf(y);
  x = floorf(x) + tx * tx * (3 - 2 * tx);
  y = floorf(y) + ty * ty * (3 - 2 * ty);
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    w->weights[i] = fmaxf(1 - fabsf(x - i % 4), 0) * fmaxf(1 - fabsf(y - i / 4), 0);
}

void fx_wobble_move(struct fx_wobble* w, float dx, float dy) {
  if (!w->grabbed || !isfinite(dx) || !isfinite(dy) || (dx == 0 && dy == 0))
    return;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
    w->displacement[i][0] -= dx;
    w->displacement[i][1] -= dy;
  }
  constrain(w);
  w->active = true;
}

void fx_wobble_release(struct fx_wobble* w) { w->grabbed = false; }

bool fx_wobble_tick(struct fx_wobble* w, double seconds) {
  if (!w->active || !isfinite(seconds) || seconds <= 0)
    return w->active;
  if (seconds > 0.25) {
    memset(w->displacement, 0, sizeof(w->displacement));
    memset(w->velocity, 0, sizeof(w->velocity));
    w->remainder = 0;
    w->active = false;
    return false;
  }
  const double step = 1.0 / 240.0;
  w->remainder += seconds;
  while (w->remainder + 1e-12 >= step) {
    w->remainder -= step;
    float acceleration[FX_WOBBLE_POINTS][2] = {0};
    for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
      int neighbours[4] = {i % 4 > 0 ? i - 1 : -1, i % 4 < 3 ? i + 1 : -1, i >= 4 ? i - 4 : -1, i < 12 ? i + 4 : -1};
      for (int axis = 0; axis < 2; axis++) {
        acceleration[i][axis] = -55 * w->displacement[i][axis] - 9.5f * w->velocity[i][axis];
        for (int n = 0; n < 4; n++) {
          if (neighbours[n] >= 0)
            acceleration[i][axis] += 160 * (w->displacement[neighbours[n]][axis] - w->displacement[i][axis]);
        }
      }
    }
    for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
      for (int axis = 0; axis < 2; axis++) {
        w->velocity[i][axis] += acceleration[i][axis] * step;
        w->displacement[i][axis] += w->velocity[i][axis] * step;
      }
    }
    constrain(w);
  }
  w->active = false;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
    for (int axis = 0; axis < 2; axis++)
      w->active |= fabsf(w->displacement[i][axis]) > 0.02f || fabsf(w->velocity[i][axis]) > 0.2f;
  }
  if (!w->active) {
    memset(w->displacement, 0, sizeof(w->displacement));
    memset(w->velocity, 0, sizeof(w->velocity));
    w->remainder = 0;
  }
  return w->active;
}
