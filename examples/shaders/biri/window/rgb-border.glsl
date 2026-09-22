// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){
        vec4 s = tex2D_screen(c.xy);

        float edge = min(min(c.x, 1.0-c.x), min(c.y, 1.0-c.y));
        float m    = clamp(biri_smoothstep(0.070, 0.0, edge) * 0.99, 0.0, 1.0);

        float ang = atan(c.y-0.5, c.x-0.5) * 0.1591549;
        float hue = fract(ang + umbriel_time*0.25);
        vec3  rgb = 0.5 + 0.5*cos(6.2831853*(hue + vec3(0.0,0.33,0.67)));

        return vec4(s.rgb + rgb*m, s.a);
    }
