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
  for (int i = 0; i < 600; i++) {
    fx_wobble_tick(&corner, 1.0 / 240);
    swung_back |= average(&corner, 0) > 0.1f;
  }
  assert(swung_back && !corner.active);
  for (int i = 0; i < FX_WOBBLE_POINTS; i++)
    for (int axis = 0; axis < 2; axis++)
      assert(corner.displacement[i][axis] == 0 && corner.velocity[i][axis] == 0);

  // Repeated shaking, extreme input and small windows remain bounded and pinned.
  fx_wobble_begin(&corner, 12, 7, 0.42f, 0.73f);
  for (int i = 0; i < 2000; i++) {
    fx_wobble_move(&corner, i % 2 ? 10000 : -10000, 37);
    fx_wobble_tick(&corner, 1.0 / 144);
    check_pin(&corner);
    for (int n = 0; n < FX_WOBBLE_POINTS; n++) {
      assert(isfinite(corner.displacement[n][0]) && isfinite(corner.displacement[n][1]));
      assert(fabsf(corner.displacement[n][0]) <= 12.0f / 18 + 0.0001f);
      assert(fabsf(corner.displacement[n][1]) <= 7.0f / 18 + 0.0001f);
    }
  }
  assert(!fx_wobble_tick(&corner, 2)); // Suspend/resume does not explode or animate stale energy.
  assert(corner.grabbed);
  fx_wobble_move(&corner, 1, 0);
  assert(corner.active);
  puts("wobble: direction, pinning, frame cadence, release oscillation, settling and bounded shaking passed");
}
