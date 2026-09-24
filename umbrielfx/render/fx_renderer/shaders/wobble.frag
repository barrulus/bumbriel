// Smooth sheet lookup with constant uniform indices for GLSL ES 1.00.
vec2 wobble_row(float x, vec2 a, vec2 b, vec2 c, vec2 d) {
    if (x < 1.0) return mix(a, b, smoothstep(0.0, 1.0, x));
    if (x < 2.0) return mix(b, c, smoothstep(0.0, 1.0, x - 1.0));
    return mix(c, d, smoothstep(0.0, 1.0, x - 2.0));
}

vec2 wobble_offset(vec2 uv) {
    vec2 p = clamp(uv, 0.0, 1.0) * 3.0;
    vec2 a = wobble_row(p.x, umbriel_wobble[0], umbriel_wobble[1], umbriel_wobble[2], umbriel_wobble[3]);
    vec2 b = wobble_row(p.x, umbriel_wobble[4], umbriel_wobble[5], umbriel_wobble[6], umbriel_wobble[7]);
    vec2 c = wobble_row(p.x, umbriel_wobble[8], umbriel_wobble[9], umbriel_wobble[10], umbriel_wobble[11]);
    vec2 d = wobble_row(p.x, umbriel_wobble[12], umbriel_wobble[13], umbriel_wobble[14], umbriel_wobble[15]);
    return wobble_row(p.y, a, b, c, d);
}

vec4 animation(vec2 uv) {
    vec2 source = uv;
    // The solver bounds the displacement gradient, making this a contraction.
    for (int i = 0; i < 10; i++) source = uv - wobble_offset(source);
    return umbriel_sample(source);
}
