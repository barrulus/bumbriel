#include <math.h>
#include <string.h>
#include <umbrielfx/render/drag_physics.h>

struct fx_drag_physics_parameters fx_drag_physics_default_parameters(void) {
  return (struct fx_drag_physics_parameters){
      .stiffness = 36, .coupling = 100, .damping = 6.5f, .pointer_response = 2,
      .motion_gain = 8, .decay = 2.8f,
  };
}

static bool in_range(float value, float low, float high) {
  return isfinite(value) && value >= low && value <= high;
}

bool fx_drag_physics_parameters_valid(const struct fx_drag_physics_parameters* p) {
  return p != NULL && in_range(p->stiffness, 1, 1000) && in_range(p->coupling, 0, 500)
      && in_range(p->damping, 0.5f, 60) && in_range(p->pointer_response, 0, 10)
      && in_range(p->stiffness_gradient, -0.9f, 1) && in_range(p->lag_gradient, -0.9f, 4)
      && in_range(p->downward_pull, 0, 50) && in_range(p->motion_gain, 0, 32) && in_range(p->decay, 0.1f, 30);
}

bool fx_drag_physics_set_parameters(struct fx_drag_physics* w, const struct fx_drag_physics_parameters* parameters) {
  if (!fx_drag_physics_parameters_valid(parameters))
    return false;
  w->parameters = *parameters;
  return true;
}

static void constrain(struct fx_drag_physics* w) {
  if (w->grabbed) {
    float sum = 0, position[2] = {0}, velocity[2] = {0};
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
      sum += w->weights[i] * w->weights[i];
      for (int axis = 0; axis < 2; axis++) {
        position[axis] += w->weights[i] * w->displacement[i][axis];
        velocity[axis] += w->weights[i] * w->velocity[i][axis];
      }
    }
    // Pin the interpolated grab point exactly, even between grid vertices.
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
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
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
    ratio = fmaxf(ratio, fabsf(w->displacement[i][0]) / fminf(200, w->width / 5));
    ratio = fmaxf(ratio, fabsf(w->displacement[i][1]) / fminf(200, w->height / 5));
    for (int axis = 0; axis < 2; axis++)
      ratio = fmaxf(ratio, fabsf(w->velocity[i][axis]) / 4000);
  }
  if (ratio > 1) {
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
      for (int axis = 0; axis < 2; axis++) {
        w->displacement[i][axis] /= ratio;
        w->velocity[i][axis] /= ratio;
      }
    }
  }
}

void fx_drag_physics_begin(struct fx_drag_physics* w, float width, float height, float grab_x, float grab_y) {
  static uint64_t serial;
  *w = (struct fx_drag_physics){
      .parameters = fx_drag_physics_default_parameters(),
      .width = fmaxf(width, 1),
      .height = fmaxf(height, 1),
      .transition_id = ++serial,
      .grabbed = true,
      .grab_y = fminf(fmaxf(grab_y, 0), 1)
  };
  float x = fminf(fmaxf(grab_x, 0), 1) * 3;
  float y = fminf(fmaxf(grab_y, 0), 1) * 3;
  float tx = x - floorf(x), ty = y - floorf(y);
  x = floorf(x) + tx * tx * (3 - 2 * tx);
  y = floorf(y) + ty * ty * (3 - 2 * ty);
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    w->weights[i] = fmaxf(1 - fabsf(x - i % 4), 0) * fmaxf(1 - fabsf(y - i / 4), 0);
  // Spread pointer response around the grab instead of creating a sharp bump
  // in just its four nearest masses. Normalize at the interpolated grab point.
  float anchor = 0;
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
    const float dx = (i % 4 - x) / 3, dy = (i / 4 - y) / 3;
    w->drag[i] = expf(-(dx * dx + dy * dy) / 0.35f);
    anchor += w->weights[i] * w->drag[i];
  }
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    w->drag[i] = 1 - w->drag[i] / anchor;
}

void fx_drag_physics_move(struct fx_drag_physics* w, float dx, float dy) {
  if (!w->grabbed || !isfinite(dx) || !isfinite(dy) || (dx == 0 && dy == 0))
    return;
  // Motion loads the hanging sheet; its weight fades away when the pointer rests.
  if (w->parameters.downward_pull > 0)
    w->stretch = fminf(1, w->stretch + hypotf(dx / w->width, dy / w->height) * w->parameters.motion_gain);
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
    const float trailing = 1 + w->parameters.lag_gradient * (i / 4) / 3;
    w->displacement[i][0] -= w->parameters.pointer_response * dx * w->drag[i] * trailing;
    w->displacement[i][1] -= w->parameters.pointer_response * dy * w->drag[i] * trailing;
  }
  constrain(w);
  w->active = true;
}

void fx_drag_physics_release(struct fx_drag_physics* w) { w->grabbed = false; }

bool fx_drag_physics_tick(struct fx_drag_physics* w, double seconds) {
  if (!w->active || !isfinite(seconds) || seconds <= 0)
    return w->active;
  if (seconds > 0.25) {
    memset(w->displacement, 0, sizeof(w->displacement));
    memset(w->velocity, 0, sizeof(w->velocity));
    w->remainder = 0;
    w->stretch = 0;
    w->active = false;
    return false;
  }
  const double step = 1.0 / 240.0;
  w->remainder += seconds;
  while (w->remainder + 1e-12 >= step) {
    w->remainder -= step;
    w->stretch *= expf(-w->parameters.decay * step);
    float acceleration[FX_DRAG_PHYSICS_POINTS][2] = {0};
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
      const float depth = (i / 4) / 3.0f;
      const float softness = 1 + w->parameters.stiffness_gradient * depth;
      int neighbours[4] = {i % 4 > 0 ? i - 1 : -1, i % 4 < 3 ? i + 1 : -1, i >= 4 ? i - 4 : -1, i < 12 ? i + 4 : -1};
      for (int axis = 0; axis < 2; axis++) {
        acceleration[i][axis] =
            -w->parameters.stiffness * softness * w->displacement[i][axis] - w->parameters.damping * w->velocity[i][axis];
        for (int n = 0; n < 4; n++) {
          if (neighbours[n] >= 0)
            acceleration[i][axis] +=
                w->parameters.coupling * (w->displacement[neighbours[n]][axis] - w->displacement[i][axis]);
        }
      }
      // The lower middle hangs furthest while the shared constraint preserves pinning.
      if (w->parameters.downward_pull > 0) {
        const float belly = i % 4 == 1 || i % 4 == 2 ? 1 : 0.65f;
        acceleration[i][1] += w->height * w->parameters.downward_pull * w->stretch * fmaxf(depth - w->grab_y, 0) * belly;
      }
    }
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
      for (int axis = 0; axis < 2; axis++) {
        w->velocity[i][axis] += acceleration[i][axis] * step;
        w->displacement[i][axis] += w->velocity[i][axis] * step;
      }
    }
    constrain(w);
  }
  w->active = w->parameters.downward_pull > 0 && w->stretch > 0.001f;
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++) {
    for (int axis = 0; axis < 2; axis++)
      w->active |= fabsf(w->displacement[i][axis]) > 0.02f || fabsf(w->velocity[i][axis]) > 0.2f;
  }
  if (!w->active) {
    memset(w->displacement, 0, sizeof(w->displacement));
    memset(w->velocity, 0, sizeof(w->velocity));
    w->remainder = 0;
    w->stretch = 0;
  }
  return w->active;
}
