// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

vec4 postprocess(vec3 c) {
    vec2 uv = c.xy;
    vec2 d = uv - 0.5;
    float r = length(d);

    float k = 0.35;
    vec2 warp = uv + d * r * r * k;

    vec2 dir = d / max(r, 1e-4);
    float ca = 0.004 + r * 0.012;
    float rC = tex2D_screen(warp + dir * ca).r;
    float gC = tex2D_screen(warp).g;
    float bC = tex2D_screen(warp - dir * ca).b;
    float a  = tex2D_screen(warp).a;

    vec3 col = vec3(rC, gC, bC);

    col *= mix(1.0, 0.70, biri_smoothstep(0.35, 0.72, r));

    return vec4(col, a);
}
