// Adapted from Biri resources/shaders/close/close-melt.kdl.
// Original shader collection by Barrulus. GPL-3.0; see LICENSE in this directory.
// Editable native Umbriel animation shader; returns premultiplied RGBA.

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    // Exact lifecycle endpoints; no native fade is applied by the host.
    if (progress <= 0.0) return umbriel_sample(uv);
    if (progress >= 1.0) return vec4(0.0);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    // Wavy per-column drip speed
    float wave1 = sin(uv.x * 12.0 + umbriel_random_seed.x * 6.28) * 0.4 + 0.6;
    float wave2 = sin(uv.x * 27.0 + umbriel_random_seed.x * 3.14) * 0.2 + 0.8;
    float drip_speed = wave1 * wave2;

    // Melt line descends from the top - pixels above it are gone
    float melt_line = progress * drip_speed * 1.3;
    if (uv.y < melt_line) {
        return vec4(0.0);
    }

    // Vertical squash: compress remaining content downward
    float squash = 1.0 - progress * 0.7;
    float remaining = 1.0 - melt_line;
    float local_y = (uv.y - melt_line) / max(remaining, 0.001);
    float new_y = melt_line + local_y * remaining * squash;

    // Horizontal stretch at bottom (puddle spreading)
    float bottom_factor = smoothstep(0.5, 1.0, uv.y);
    float h_stretch = 1.0 + progress * 0.4 * bottom_factor;
    float new_x = 0.5 + (uv.x - 0.5) / h_stretch;

    // Wavy distortion at the melting edge
    float wave_distort = sin(uv.x * 15.0 + progress * 8.0) * 0.008 * progress;
    new_y += wave_distort;

    if (new_x < 0.0 || new_x > 1.0 || new_y < 0.0 || new_y > 1.0) {
        return vec4(0.0);
    }

    vec4 color = umbriel_sample(vec3(new_x, new_y, 1.0).xy);

    // Fade out in the final stretch
    float fade = 1.0 - smoothstep(0.6, 1.0, progress);

    // Heat shimmer tint near the melting front
    float heat_zone = (1.0 - smoothstep(melt_line, melt_line + 0.15, uv.y));
    vec3 heat_tint = mix(color.rgb, vec3(1.0, 0.6, 0.2) * color.a, heat_zone * 0.3 * progress);

    return vec4(heat_tint * fade, color.a * fade);
}
