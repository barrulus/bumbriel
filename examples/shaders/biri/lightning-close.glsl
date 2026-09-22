// Adapted from Biri resources/shaders/close/close-lightning.kdl; GPL-3.0-only, see LICENSE.
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; i++) {
        value += amp * noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return value;
}

vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    if (progress <= 0.0) return umbriel_sample(uv);
    if (progress >= 1.0) return vec4(0.0);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    vec2 center = vec2(0.5, 0.5);
    float dist = distance(uv, center);

    vec2 noise_uv = uv * 8.0 + vec2(umbriel_random_seed.x * 100.0);

    float noise_val = fbm(noise_uv) * 0.35;
    float burn_radius = progress * 1.55 - 0.35 + noise_val;

    vec4 color = umbriel_sample(uv);

    float edge_width = 0.08;
    float inner_edge = burn_radius - edge_width;

    if (dist < inner_edge) {
        return vec4(0.0);
    } else if (dist < burn_radius) {
        float edge_progress = (dist - inner_edge) / edge_width;

        vec3 lightning_white = vec3(1.0, 1.0, 1.0);
        vec3 lightning_cyan = vec3(0.3, 0.8, 1.0);
        vec3 lightning_blue = vec3(0.2, 0.3, 1.0);

        vec3 glow_color = mix(lightning_white, lightning_cyan, edge_progress);
        glow_color = mix(glow_color, lightning_blue, edge_progress * edge_progress);

        float flicker = 0.8 + 0.2 * noise(uv * 20.0 + vec2(progress * 50.0));
        float glow_alpha = flicker * (1.0 - edge_progress * 0.3);

        return vec4(glow_color * glow_alpha, glow_alpha);
    } else {
        float distortion = fbm(noise_uv + vec2(progress * 5.0)) * 0.005;
        float proximity = 1.0 - smoothstep(burn_radius, burn_radius + 0.15, dist);
        vec2 distorted_uv = uv + distortion * proximity * vec2(1.0, 1.0);

        if (distorted_uv.x < 0.0 || distorted_uv.x > 1.0 ||
            distorted_uv.y < 0.0 || distorted_uv.y > 1.0) {
            return vec4(0.0);
        }

        vec4 dcolor = umbriel_sample(vec3(distorted_uv, 1.0).xy);

        vec3 tinted = mix(dcolor.rgb, vec3(0.5, 0.8, 1.0) * dcolor.a, proximity * 0.15);
        return vec4(tinted, dcolor.a);
    }
}
