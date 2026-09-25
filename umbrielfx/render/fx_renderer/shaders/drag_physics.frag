// One bicubic surface couples the whole window, with constant uniform indices
// for GLSL ES 1.00. The CPU pins the cursor with the same Bernstein weights.
vec2 physics_row(float t, vec2 a, vec2 b, vec2 c, vec2 d) {
    float u = 1.0 - t;
    return u * u * (u * a + 3.0 * t * b) + t * t * (3.0 * u * c + t * d);
}

vec2 physics_offset(vec2 uv) {
    vec2 p = clamp(uv, 0.0, 1.0);
    vec2 a = physics_row(p.x, umbriel_deformation[0], umbriel_deformation[1], umbriel_deformation[2], umbriel_deformation[3]);
    vec2 b = physics_row(p.x, umbriel_deformation[4], umbriel_deformation[5], umbriel_deformation[6], umbriel_deformation[7]);
    vec2 c = physics_row(p.x, umbriel_deformation[8], umbriel_deformation[9], umbriel_deformation[10], umbriel_deformation[11]);
    vec2 d = physics_row(p.x, umbriel_deformation[12], umbriel_deformation[13], umbriel_deformation[14], umbriel_deformation[15]);
    return physics_row(p.y, a, b, c, d);
}

vec4 animation(vec2 uv) {
    vec2 source = uv;
    // The solver bounds the displacement gradient, making this a contraction.
    for (int i = 0; i < 28; i++) source = uv - physics_offset(source);
    return umbriel_sample(source);
}
