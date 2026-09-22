// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){ vec3 s=tex2D_screen(c.xy).rgb; float v=biri_smoothstep(0.85,0.35,length(c.xy-0.5)); return vec4(s*mix(0.5,1.0,v), 1.0); }
