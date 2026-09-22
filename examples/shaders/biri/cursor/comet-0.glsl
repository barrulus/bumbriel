// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){
            vec3 prev = max(tex2D_prev(c.xy).rgb * 0.98 - 0.004, vec3(0.0));
            float hue = fract(umbriel_time*0.3);
            vec3 rb = 0.5 + 0.5*cos(6.2831*(hue + vec3(0.0,0.33,0.67)));
            float d = length(c.xy*umbriel_output_size - umbriel_cursor);
            float fresh = biri_smoothstep(40.0, 0.0, d)*0.9;
            return vec4(max(prev, rb*fresh), 1.0);
        }
