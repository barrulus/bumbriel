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
  // Limit actual neighbouring slopes instead of capping every mass at 1/24
  // of the window. Broad bends can travel much farther without folding.
  // Smoothstep's maximum derivative is 1.5, over three cells per axis.
  // Bound each normalized Jacobian row sum to keep inverse sampling contractive.
  float ratio = 1;
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 3; x++) {
      const int a = y * 4 + x, b = a + 1, c = a + 4, d = c + 1;
      for (int axis = 0; axis < 2; axis++) {
        const float extent = axis == 0 ? w->width : w->height;
        const float horizontal = fmaxf(
            fabsf(w->displacement[b][axis] - w->displacement[a][axis]),
            fabsf(w->displacement[d][axis] - w->displacement[c][axis])
        );
        const float vertical = fmaxf(
            fabsf(w->displacement[c][axis] - w->displacement[a][axis]),
            fabsf(w->displacement[d][axis] - w->displacement[b][axis])
        );
        ratio = fmaxf(ratio, 4.5f * (horizontal + vertical) / (extent * 0.7f));
      }
    }
  }
  // A global scale preserves the pin constraint, unlike clamping each mass.
  for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
    ratio = fmaxf(ratio, fabsf(w->displacement[i][0]) / fminf(200, w->width / 5));
    ratio = fmaxf(ratio, fabsf(w->displacement[i][1]) / fminf(200, w->height / 5));
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
  // Spread pointer response around the grab instead of creating a sharp bump
  // in just its four nearest masses. Normalize at the interpolated grab point.
  float anchor = 0;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
    const float dx = (i % 4 - x) / 3, dy = (i / 4 - y) / 3;
    w->drag[i] = expf(-(dx * dx + dy * dy) / 0.35f);
    anchor += w->weights[i] * w->drag[i];
  }
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    w->drag[i] = 1 - w->drag[i] / anchor;
}

void fx_wobble_move(struct fx_wobble* w, float dx, float dy) {
  if (!w->grabbed || !isfinite(dx) || !isfinite(dy) || (dx == 0 && dy == 0))
    return;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++) {
    w->displacement[i][0] -= 2 * dx * w->drag[i];
    w->displacement[i][1] -= 2 * dy * w->drag[i];
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
        acceleration[i][axis] = -36 * w->displacement[i][axis] - 6.5f * w->velocity[i][axis];
        for (int n = 0; n < 4; n++) {
          if (neighbours[n] >= 0)
            acceleration[i][axis] += 100 * (w->displacement[neighbours[n]][axis] - w->displacement[i][axis]);
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
