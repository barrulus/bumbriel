float rounded_rect_distance(
    vec2 size, vec2 position, float radius_tl, float radius_tr, float radius_bl, float radius_br
) {
  vec2 relative_pos = gl_FragCoord.xy - position;
  vec2 centered_pos = relative_pos - size * 0.5;

  float radius;
  if (centered_pos.x < 0.0) {
    radius = centered_pos.y < 0.0 ? radius_tl : radius_bl;
  } else {
    radius = centered_pos.y < 0.0 ? radius_tr : radius_br;
  }

  vec2 q = abs(centered_pos) - size * 0.5 + radius;
  return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

// Returns 0.0 outside and 1.0 inside the rounded rectangle. A cutout reverses
// the result so the rectangle interior becomes transparent.
float corner_alpha(
    vec2 size, vec2 position, bool is_cutout, float radius_tl, float radius_tr, float radius_bl, float radius_br
) {
  float dist = rounded_rect_distance(size, position, radius_tl, radius_tr, radius_bl, radius_br);
  float result = smoothstep(0.0, 1.0, dist);
  return is_cutout ? result : 1.0 - result;
}
