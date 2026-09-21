// Adapted from Biri resources/shaders/close/close-ripple.kdl.
// Original shader collection by Barrulus. GPL-3.0; see LICENSE in this directory.
// Editable native Umbriel animation shader; returns premultiplied RGBA.

vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    // Exact lifecycle endpoints; no native fade is applied by the host.
    if (progress <= 0.0) return vec4(0.0);
    if (progress >= 1.0) return umbriel_sample(uv);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    vec2 center = vec2(0.5, 0.5);
    float dist = distance(uv, center);

    // Ripples converge inward as window materialises
    float wave_count = 4.0;
    float amplitude = 0.012 * smoothstep(0.0, 0.3, progress) * (1.0 - smoothstep(0.5, 1.0, progress));
    float speed = (1.0 - progress) * 6.0;

    float wave = sin(dist * wave_count * 6.28318 - speed) * amplitude;
    wave *= smoothstep(0.0, 0.15, dist);

    vec2 dir = normalize(uv - center + vec2(0.0001));
    vec2 displaced_uv = uv + dir * wave;

    if (displaced_uv.x < 0.0 || displaced_uv.x > 1.0 ||
        displaced_uv.y < 0.0 || displaced_uv.y > 1.0) {
        return vec4(0.0);
    }

    vec4 color = umbriel_sample(vec3(displaced_uv, 1.0).xy);

    // Fade in from edges toward center
    float fade = smoothstep(0.0, 0.6, progress);
    // Advance the soft reveal edge beyond every corner by completion.
    float ring_reveal = 1.0 - smoothstep(progress * 0.8, progress * 0.8 + 0.1, dist);
    fade *= mix(1.0, ring_reveal, smoothstep(0.0, 0.5, progress));

    return color * fade;
}
