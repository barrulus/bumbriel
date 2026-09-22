// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){ vec4 s=tex2D_screen(c.xy); float d=length(c.xy*umbriel_output_size-umbriel_cursor); float k=biri_smoothstep(320.0,120.0,d); return vec4(s.rgb*mix(0.35,1.0,k), s.a); }
