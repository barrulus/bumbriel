// Adapted from Biri resources/shaders/close/close-whirlpool.kdl; GPL-3.0-only, see LICENSE.
vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    if (progress <= 0.0) return umbriel_sample(uv);
    if (progress >= 1.0) return vec4(0.0);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    vec2 center = vec2(0.5, 0.5);
    vec2 delta = uv - center;
    float dist = length(delta);
    float angle = atan(delta.y, delta.x);

    float max_rotation = 6.28318 * 1.5;
    float rotation = progress * max_rotation * (1.0 - dist * 0.5);

    float pull = 1.0 - progress * 0.8;
    float new_dist = dist * pull;

    float new_angle = angle + rotation;
    vec2 new_uv = center + new_dist * vec2(cos(new_angle), sin(new_angle));

    if (new_uv.x < 0.0 || new_uv.x > 1.0 ||
        new_uv.y < 0.0 || new_uv.y > 1.0) {
        return vec4(0.0);
    }

    vec4 color = umbriel_sample(vec3(new_uv, 1.0).xy);

    float fade = 1.0 - smoothstep(0.1, 0.9, progress);

    float dissolve = 1.0 - smoothstep(max(0.0, 0.72 - progress),
        max(0.001, 1.0 - progress), dist);

    return color * fade * dissolve;
}
