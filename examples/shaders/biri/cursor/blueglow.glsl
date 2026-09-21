// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges in the original are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){ vec3 s=tex2D_screen(c.xy).rgb; float d=length(c.xy*umbriel_output_size-umbriel_cursor); float ring=biri_smoothstep(7.0,0.0,abs(d-55.0)); float glow=biri_smoothstep(110.0,0.0,d)*0.3; float m=clamp(ring+glow,0.0,1.0); return vec4(mix(s, vec3(0.15,0.5,1.0), m*0.85), 1.0); }
