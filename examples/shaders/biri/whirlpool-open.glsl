// Adapted from Biri resources/shaders/close/close-whirlpool.kdl; GPL-3.0-only, see LICENSE.
vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    if (progress <= 0.0) return vec4(0.0);
    if (progress >= 1.0) return umbriel_sample(uv);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    vec2 center = vec2(0.5, 0.5);
    vec2 delta = uv - center;
    float dist = length(delta);
    float angle = atan(delta.y, delta.x);

    float inv = 1.0 - progress;
    float max_rotation = 6.28318 * 1.5;
    float rotation = inv * max_rotation * (1.0 - dist * 0.5);

    float pull = mix(0.2, 1.0, progress);
    float new_dist = dist / max(pull, 0.001);

    float new_angle = angle + rotation;
    vec2 new_uv = center + new_dist * vec2(cos(new_angle), sin(new_angle));

    if (new_uv.x < 0.0 || new_uv.x > 1.0 ||
        new_uv.y < 0.0 || new_uv.y > 1.0) {
        return vec4(0.0);
    }

    vec4 color = umbriel_sample(vec3(new_uv, 1.0).xy);

    float fade = smoothstep(0.0, 0.5, progress);

    return color * fade;
}
