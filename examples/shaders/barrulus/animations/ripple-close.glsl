// Shader by Barrulus, adapted for Umbriel.
vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    if (progress <= 0.0) return umbriel_sample(uv);
    if (progress >= 1.0) return vec4(0.0);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    vec2 center = vec2(0.5, 0.5);
    float dist = distance(uv, center);

    float wave_count = 4.0;
    float amplitude = 0.015 * smoothstep(0.0, 0.3, progress) * (1.0 - smoothstep(0.4, 1.0, progress));
    float speed = progress * 6.0;

    float wave = sin(dist * wave_count * 6.28318 - speed) * amplitude;
    wave *= smoothstep(0.0, 0.15, dist);

    vec2 dir = normalize(uv - center + vec2(0.0001));
    vec2 displaced_uv = uv + dir * wave;

    if (displaced_uv.x < 0.0 || displaced_uv.x > 1.0 ||
        displaced_uv.y < 0.0 || displaced_uv.y > 1.0) {
        return vec4(0.0);
    }

    vec4 color = umbriel_sample(vec3(displaced_uv, 1.0).xy);

    float fade = 1.0 - smoothstep(0.2, 1.0, progress);
    float ring_fade = smoothstep(0.0, progress * 0.8 + 0.1, dist);
    fade *= mix(1.0, 1.0 - ring_fade, smoothstep(0.1, 0.7, progress));

    return color * fade;
}
