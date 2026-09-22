// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

vec4 postprocess(vec3 c) {
    vec2 uv = c.xy;

    vec2 cc = uv - 0.5;
    float r2 = dot(cc, cc);
    vec2 warp = uv + cc * r2 * 0.18;

    vec2 inb = step(vec2(0.0), warp) * step(warp, vec2(1.0));
    float mask = inb.x * inb.y;

    float ca = 0.0016 + r2 * 0.004;
    vec3 col;
    col.r = tex2D_screen(warp + cc * ca).r;
    col.g = tex2D_screen(warp).g;
    col.b = tex2D_screen(warp - cc * ca).b;

    float sl = 0.5 + 0.5 * sin(warp.y * umbriel_size.y * 3.14159);
    col *= mix(0.72, 1.0, sl);

    float triad = mod(floor(warp.x * umbriel_size.x), 3.0);
    vec3 grille = vec3(0.5);
    if (triad < 1.0) grille.r = 1.0;
    else if (triad < 2.0) grille.g = 1.0;
    else grille.b = 1.0;
    col *= mix(vec3(1.0), grille, 0.35);

    float vig = biri_smoothstep(0.85, 0.20, r2 * 2.0);
    col *= mix(0.45, 1.18, vig);

    col *= mask;
    return vec4(col, 1.0);
}
