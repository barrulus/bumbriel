// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){ vec2 px=c.xy*umbriel_output_size; vec2 d=px-umbriel_cursor; float r=length(d); float pulse=200.0+40.0*sin(umbriel_time*4.0); float band=biri_smoothstep(45.0,0.0,abs(r-pulse)); float disp=sin(r*0.3-umbriel_time*8.0)*5.0*band; vec3 scene=tex2D_screen((px+normalize(d+1e-3)*disp)/umbriel_output_size).rgb; float hue=fract(umbriel_time*0.15); vec3 rb=0.5+0.5*cos(6.2831*(hue+vec3(0.0,0.33,0.67))); float ring=biri_smoothstep(26.0,0.0,abs(r-pulse)); float glow=biri_smoothstep(pulse,0.0,r)*0.20; float core=biri_smoothstep(55.0,0.0,r); float m=clamp(ring*0.9+glow+core*0.7,0.0,1.0); return vec4(mix(scene,rb,m),1.0); }
