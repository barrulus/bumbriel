#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <umbrielfx/render/drag_physics.h>

static void check_pin(const struct fx_drag_physics* w) {
  for (int axis = 0; axis < 2; axis++) {
    float sum = 0;
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
      sum += w->weights[i] * w->displacement[i][axis];
    assert(fabsf(sum) < 0.0001f);
  }
}

static float average(const struct fx_drag_physics* w, int axis) {
  float sum = 0;
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    sum += w->displacement[i][axis];
  return sum / FX_DRAG_PHYSICS_POINTS;
}

static void offset_at(const struct fx_drag_physics* w, const float uv[2], float offset[2]) {
  const float x = fminf(fmaxf(uv[0], 0), 1) * 3, y = fminf(fmaxf(uv[1], 0), 1) * 3;
  const int col = (int)fminf(floorf(x), 2), row = (int)fminf(floorf(y), 2);
  const float tx = x - col, ty = y - row;
  const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
  for (int axis = 0; axis < 2; axis++) {
    const int a = row * 4 + col;
    const float top = w->displacement[a][axis] * (1 - sx) + w->displacement[a + 1][axis] * sx;
    const float bottom = w->displacement[a + 4][axis] * (1 - sx) + w->displacement[a + 5][axis] * sx;
    offset[axis] = (top * (1 - sy) + bottom * sy) / (axis == 0 ? w->width : w->height);
  }
}

// The shader must recover the original texel even at the stronger drag limit.
static void check_inverse(const struct fx_drag_physics* w) {
  for (int y = 0; y <= 8; y++) {
    for (int x = 0; x <= 8; x++) {
      const float original[2] = {x / 8.0f, y / 8.0f};
      float offset[2];
      offset_at(w, original, offset);
      const float rendered[2] = {original[0] + offset[0], original[1] + offset[1]};
      float source[2] = {rendered[0], rendered[1]};
      for (int i = 0; i < 28; i++) {
        offset_at(w, source, offset);
        for (int axis = 0; axis < 2; axis++)
          source[axis] = rendered[axis] - offset[axis];
      }
      for (int axis = 0; axis < 2; axis++)
        assert(fabsf(source[axis] - original[axis]) < 0.00005f);
    }
  }
}

static struct fx_drag_physics_parameters taffy_parameters(void) {
  struct fx_drag_physics_parameters parameters = fx_drag_physics_default_parameters();
  parameters.coupling = 48;
  parameters.damping = 4.8f;
  parameters.stiffness_gradient = -0.55f;
  parameters.lag_gradient = 0.9f;
  parameters.downward_pull = 18;
  return parameters;
}

int main(void) {
  struct fx_drag_physics right, left, diagonal;
  fx_drag_physics_begin(&right, 600, 400, 0.17f, 0.23f);
  fx_drag_physics_begin(&left, 600, 400, 0.17f, 0.23f);
  fx_drag_physics_begin(&diagonal, 600, 400, 0.17f, 0.23f);
  assert(!fx_drag_physics_tick(&right, 1.0 / 60));
  fx_drag_physics_move(&right, 12, 0);
  fx_drag_physics_move(&left, -12, 0);
  fx_drag_physics_move(&diagonal, 8, -8);
  assert(average(&right, 0) < 0 && average(&left, 0) > 0);
  assert(average(&diagonal, 0) < 0 && average(&diagonal, 1) > 0);
  check_pin(&right);
  check_pin(&diagonal);
  for (int frame = 0; frame < 30; frame++) {
    fx_drag_physics_tick(&right, 1.0 / 60);
    fx_drag_physics_tick(&left, 1.0 / 60);
    check_pin(&right);
    for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
      assert(fabsf(right.displacement[i][0] + left.displacement[i][0]) < 0.0001f);
  }

  // Same input timestamps and elapsed time at 60/120Hz produce the same sheet.
  struct fx_drag_physics slow, fast;
  fx_drag_physics_begin(&slow, 600, 400, 0.5f, 0.08f);
  fx_drag_physics_move(&slow, 15, -6);
  fast = slow;
  for (int i = 0; i < 30; i++)
    fx_drag_physics_tick(&slow, 1.0 / 60);
  for (int i = 0; i < 60; i++)
    fx_drag_physics_tick(&fast, 1.0 / 120);
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(fabsf(slow.displacement[i][axis] - fast.displacement[i][axis]) < 0.0001f);

  // A corner grab holds that corner while the opposite corner trails it.
  struct fx_drag_physics corner;
  fx_drag_physics_begin(&corner, 600, 400, 0, 0);
  fx_drag_physics_move(&corner, 10, 0);
  assert(corner.displacement[0][0] == 0 && corner.displacement[15][0] < -9);
  fx_drag_physics_release(&corner);
  assert(corner.active);
  bool swung_back = false;
  for (int i = 0; i < 1200; i++) {
    fx_drag_physics_tick(&corner, 1.0 / 240);
    swung_back |= average(&corner, 0) > 0.1f;
  }
  assert(swung_back && !corner.active);
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(corner.displacement[i][axis] == 0 && corner.velocity[i][axis] == 0);

  // A decisive drag produces a substantial bend, beyond the old 25px cap.
  fx_drag_physics_begin(&corner, 600, 400, 0.17f, 0.23f);
  fx_drag_physics_move(&corner, 60, 0);
  float excursion = 0;
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    excursion = fmaxf(excursion, fabsf(corner.displacement[i][0]));
  assert(excursion > 75);
  check_pin(&corner);
  check_inverse(&corner);

  // Repeated shaking, extreme input and small windows remain bounded and pinned.
  fx_drag_physics_begin(&corner, 12, 7, 0.42f, 0.73f);
  for (int i = 0; i < 2000; i++) {
    fx_drag_physics_move(&corner, i % 2 ? 10000 : -10000, 37);
    fx_drag_physics_tick(&corner, 1.0 / 144);
    check_pin(&corner);
    if (i % 100 == 0)
      check_inverse(&corner);
    for (int n = 0; n < FX_DRAG_PHYSICS_POINTS; n++) {
      assert(isfinite(corner.displacement[n][0]) && isfinite(corner.displacement[n][1]));
      assert(fabsf(corner.displacement[n][0]) <= 12.0f / 5 + 0.0001f);
      assert(fabsf(corner.displacement[n][1]) <= 7.0f / 5 + 0.0001f);
    }
  }
  assert(!fx_drag_physics_tick(&corner, 2)); // Suspend/resume does not explode or animate stale energy.
  assert(corner.grabbed);
  fx_drag_physics_move(&corner, 1, 0);
  assert(corner.active);
  // Taffy's lower half trails further, then develops a downward, curved belly.
  struct fx_drag_physics jelly, taffy;
  fx_drag_physics_begin(&jelly, 600, 400, 0.5f, 0.08f);
  taffy = jelly;
  struct fx_drag_physics_parameters parameters = taffy_parameters();
  assert(fx_drag_physics_set_parameters(&taffy, &parameters));
  fx_drag_physics_move(&jelly, 30, 0);
  fx_drag_physics_move(&taffy, 30, 0);
  assert(taffy.displacement[13][0] < jelly.displacement[13][0] * 1.5f);
  slow = taffy;
  fast = taffy;
  for (int i = 0; i < 20; i++) {
    fx_drag_physics_tick(&taffy, 1.0 / 60);
    check_pin(&taffy);
    check_inverse(&taffy);
  }
  assert(taffy.displacement[13][1] > 30);
  assert(taffy.displacement[13][1] > taffy.displacement[12][1] + 3);
  for (int i = 0; i < 30; i++)
    fx_drag_physics_tick(&slow, 1.0 / 60);
  for (int i = 0; i < 60; i++)
    fx_drag_physics_tick(&fast, 1.0 / 120);
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(fabsf(slow.displacement[i][axis] - fast.displacement[i][axis]) < 0.0001f);
  fx_drag_physics_release(&taffy);
  swung_back = false;
  for (int i = 0; i < 1800; i++) {
    fx_drag_physics_tick(&taffy, 1.0 / 240);
    swung_back |= taffy.displacement[13][0] > 1;
  }
  assert(swung_back && !taffy.active);
  // Resting while still held must also settle, without permanent gravity damage.
  for (int i = 0; i < 1800; i++)
    fx_drag_physics_tick(&slow, 1.0 / 240);
  assert(!slow.active && slow.grabbed);
  for (int i = 0; i < FX_DRAG_PHYSICS_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(taffy.displacement[i][axis] == 0 && slow.displacement[i][axis] == 0);
  // Bottom and interior grabs still pin correctly under extreme repeated input.
  for (int grab = 0; grab < 3; grab++) {
    fx_drag_physics_begin(&taffy, 12, 7, 0.42f, grab * 0.5f);
    assert(fx_drag_physics_set_parameters(&taffy, &parameters));
    for (int i = 0; i < 300; i++) {
      fx_drag_physics_move(&taffy, i % 2 ? 10000 : -10000, 37);
      fx_drag_physics_tick(&taffy, 1.0 / 144);
      check_pin(&taffy);
      check_inverse(&taffy);
    }
    assert(!fx_drag_physics_tick(&taffy, 2) && taffy.stretch == 0);
  }
  fx_drag_physics_begin(&taffy, 600, 400, 0.42f, 0.1f);
  assert(fx_drag_physics_set_parameters(&taffy, &parameters));
  fx_drag_physics_move(&taffy, 30, 5);
  fx_drag_physics_tick(&taffy, 1.0 / 60);
  struct fx_drag_physics retained = taffy;
  parameters.downward_pull = 10;
  parameters.decay = 5;
  assert(fx_drag_physics_set_parameters(&taffy, &parameters));
  assert(taffy.transition_id == retained.transition_id && taffy.grabbed);
  assert(memcmp(taffy.displacement, retained.displacement, sizeof(taffy.displacement)) == 0);
  assert(memcmp(taffy.velocity, retained.velocity, sizeof(taffy.velocity)) == 0);
  assert(taffy.stretch == retained.stretch);
  parameters.damping = 0;
  assert(!fx_drag_physics_set_parameters(&taffy, &parameters));
  assert(taffy.parameters.damping == retained.parameters.damping);
  for (int extreme = 0; extreme < 2; ++extreme) {
    parameters = (struct fx_drag_physics_parameters){
        .stiffness = extreme ? 1000 : 1, .coupling = extreme ? 500 : 0,
        .damping = extreme ? 60 : 0.5f, .pointer_response = 10,
        .stiffness_gradient = extreme ? 1 : -0.9f, .lag_gradient = 4,
        .downward_pull = 50, .motion_gain = 32, .decay = extreme ? 30 : 0.1f,
    };
    fx_drag_physics_begin(&taffy, 600, 400, 0.4f, 0.4f);
    assert(fx_drag_physics_set_parameters(&taffy, &parameters));
    for (int i = 0; i < 1000; ++i) {
      fx_drag_physics_move(&taffy, i % 2 ? 10000 : -10000, 10000);
      fx_drag_physics_tick(&taffy, 1.0 / 60);
      check_pin(&taffy);
      check_inverse(&taffy);
    }
    fx_drag_physics_release(&taffy);
    assert(!fx_drag_physics_tick(&taffy, 2));
  }
  puts("physics: jelly and taffy pinning, droop, cadence, inverse sampling, swing, settling and shaking passed");
}
