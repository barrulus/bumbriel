// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

vec4 postprocess(vec3 c) {
    float block = 0.8;
    float levels = 6.0;

    vec2 px = c.xy * umbriel_size;
    vec2 cell = floor(px / block);

    vec2 uvq = (cell + 0.5) * block / umbriel_size;
    vec4 s = tex2D_screen(uvq);

    vec3 col = floor(s.rgb * levels + 0.5) / levels;

    vec2 g = fract(px / block);
    float line = min(biri_smoothstep(0.0, 0.06, g.x), biri_smoothstep(0.0, 0.06, g.y));
    col *= mix(0.82, 1.0, line);

    return vec4(col, s.a);
}
