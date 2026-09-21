// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges in the original are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){
        vec3 s  = tex2D_screen(c.xy).rgb;
        vec2 px = c.xy*umbriel_output_size;
        float d = length(px - umbriel_cursor);

        // rainbow tunnel inside the ring — concentric bands flowing inward
        float t = d*0.07 + umbriel_time*1.0;                                 // inward flow (toward cursor)
        vec3 tunnel = 0.5 + 0.5*cos(6.2831853*(t + vec3(0.0,0.33,0.67))); // IQ rainbow palette
        float fill  = biri_smoothstep(54.0, 50.0, d);                          // fill interior, fade at rim
        vec3 outc   = mix(s, tunnel, fill*0.09);                          // light, see-through blend

        // adaptive luminance ring + glow, drawn on top
        float ring = biri_smoothstep(7.0,0.0,abs(d-55.0));
        float glow = biri_smoothstep(110.0,0.0,d)*0.3;
        float lum  = dot(s, vec3(0.299,0.587,0.114));
        vec3 ringCol = mix(vec3(0.35,0.75,1.0), vec3(0.0,0.1,0.45), biri_smoothstep(0.4,0.6,lum));
        float m = clamp(ring+glow,0.0,1.0);

        return vec4(mix(outc, ringCol, m*0.9), 1.0);
    }
