// Shader by Barrulus, adapted for Umbriel.
vec4 animation(vec2 uv) {
    float progress = umbriel_clamped_progress;

    if (progress <= 0.0) return vec4(0.0);
    if (progress >= 1.0) return umbriel_sample(uv);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }

    float inv = 1.0 - progress;

    float squash = 0.3 + 0.7 * progress;
    float new_y = 1.0 - (1.0 - uv.y) / squash;

    float h_stretch = 1.0 + inv * 0.4 * smoothstep(0.5, 1.0, uv.y);
    float new_x = 0.5 + (uv.x - 0.5) * h_stretch;

    float wave = sin(uv.x * 15.0 + inv * 8.0) * 0.01 * inv;
    new_y += wave;

    if (new_x < 0.0 || new_x > 1.0 || new_y < 0.0 || new_y > 1.0) {
        return vec4(0.0);
    }

    vec4 color = umbriel_sample(vec3(new_x, new_y, 1.0).xy);

    float fade = smoothstep(0.0, 0.5, progress);

    return color * fade;
}
