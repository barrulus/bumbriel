#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <umbrielfx/render/wobble.h>

static void check_pin(const struct fx_wobble* w) {
  for (int axis = 0; axis < 2; axis++) {
    float sum = 0;
    for (int i = 0; i < FX_WOBBLE_POINTS; i++)
      sum += w->weights[i] * w->displacement[i][axis];
    assert(fabsf(sum) < 0.0001f);
  }
}

static float average(const struct fx_wobble* w, int axis) {
  float sum = 0;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    sum += w->displacement[i][axis];
  return sum / FX_WOBBLE_POINTS;
}

static void offset_at(const struct fx_wobble* w, const float uv[2], float offset[2]) {
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
static void check_inverse(const struct fx_wobble* w) {
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

int main(void) {
  struct fx_wobble right, left, diagonal;
  fx_wobble_begin(&right, 600, 400, 0.17f, 0.23f);
  fx_wobble_begin(&left, 600, 400, 0.17f, 0.23f);
  fx_wobble_begin(&diagonal, 600, 400, 0.17f, 0.23f);
  assert(!fx_wobble_tick(&right, 1.0 / 60));
  fx_wobble_move(&right, 12, 0);
  fx_wobble_move(&left, -12, 0);
  fx_wobble_move(&diagonal, 8, -8);
  assert(average(&right, 0) < 0 && average(&left, 0) > 0);
  assert(average(&diagonal, 0) < 0 && average(&diagonal, 1) > 0);
  check_pin(&right);
  check_pin(&diagonal);
  for (int frame = 0; frame < 30; frame++) {
    fx_wobble_tick(&right, 1.0 / 60);
    fx_wobble_tick(&left, 1.0 / 60);
    check_pin(&right);
    for (int i = 0; i < FX_WOBBLE_POINTS; i++)
      assert(fabsf(right.displacement[i][0] + left.displacement[i][0]) < 0.0001f);
  }

  // Same input timestamps and elapsed time at 60/120Hz produce the same sheet.
  struct fx_wobble slow, fast;
  fx_wobble_begin(&slow, 600, 400, 0.5f, 0.08f);
  fx_wobble_move(&slow, 15, -6);
  fast = slow;
  for (int i = 0; i < 30; i++)
    fx_wobble_tick(&slow, 1.0 / 60);
  for (int i = 0; i < 60; i++)
    fx_wobble_tick(&fast, 1.0 / 120);
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(fabsf(slow.displacement[i][axis] - fast.displacement[i][axis]) < 0.0001f);

  // A corner grab holds that corner while the opposite corner trails it.
  struct fx_wobble corner;
  fx_wobble_begin(&corner, 600, 400, 0, 0);
  fx_wobble_move(&corner, 10, 0);
  assert(corner.displacement[0][0] == 0 && corner.displacement[15][0] < -9);
  fx_wobble_release(&corner);
  assert(corner.active);
  bool swung_back = false;
  for (int i = 0; i < 1200; i++) {
    fx_wobble_tick(&corner, 1.0 / 240);
    swung_back |= average(&corner, 0) > 0.1f;
  }
  assert(swung_back && !corner.active);
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(corner.displacement[i][axis] == 0 && corner.velocity[i][axis] == 0);

  // A decisive drag produces a substantial bend, beyond the old 25px cap.
  fx_wobble_begin(&corner, 600, 400, 0.17f, 0.23f);
  fx_wobble_move(&corner, 60, 0);
  float excursion = 0;
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    excursion = fmaxf(excursion, fabsf(corner.displacement[i][0]));
  assert(excursion > 75);
  check_pin(&corner);
  check_inverse(&corner);

  // Repeated shaking, extreme input and small windows remain bounded and pinned.
  fx_wobble_begin(&corner, 12, 7, 0.42f, 0.73f);
  for (int i = 0; i < 2000; i++) {
    fx_wobble_move(&corner, i % 2 ? 10000 : -10000, 37);
    fx_wobble_tick(&corner, 1.0 / 144);
    check_pin(&corner);
    if (i % 100 == 0)
      check_inverse(&corner);
    for (int n = 0; n < FX_WOBBLE_POINTS; n++) {
      assert(isfinite(corner.displacement[n][0]) && isfinite(corner.displacement[n][1]));
      assert(fabsf(corner.displacement[n][0]) <= 12.0f / 5 + 0.0001f);
      assert(fabsf(corner.displacement[n][1]) <= 7.0f / 5 + 0.0001f);
    }
  }
  assert(!fx_wobble_tick(&corner, 2)); // Suspend/resume does not explode or animate stale energy.
  assert(corner.grabbed);
  fx_wobble_move(&corner, 1, 0);
  assert(corner.active);
  puts("wobble: direction, pinning, frame cadence, release oscillation, settling and bounded shaking passed");
}
