// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess_buffer(vec3 c){
        float prev = tex2D_buffer(c.xy).r;
        float d = length(c.xy*umbriel_output_size - umbriel_cursor);
        float fresh = biri_smoothstep(18.0, 0.0, d)*0.8;
        float decayed = max(prev*0.85 - 0.006, 0.0);
        return vec4(vec3(max(decayed, fresh)), 1.0);
    }
    vec4 postprocess(vec3 c){
        vec3 s = tex2D_screen(c.xy).rgb;
        vec3 tcol = vec3(0.2, 0.8, 1.0);
        float t = tex2D_buffer(c.xy).r;
        return vec4(mix(s, tcol, t), 1.0);
    }
