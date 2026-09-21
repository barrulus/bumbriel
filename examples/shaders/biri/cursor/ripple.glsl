// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges in the original are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){ vec2 px=c.xy*umbriel_output_size; vec2 d=px-umbriel_cursor; float r=length(d); float w=biri_smoothstep(130.0,0.0,r); float disp=sin(r*0.12-umbriel_time*7.0)*8.0*w; vec2 uv=(px+normalize(d+1e-3)*disp)/umbriel_output_size; return tex2D_screen(uv); }
