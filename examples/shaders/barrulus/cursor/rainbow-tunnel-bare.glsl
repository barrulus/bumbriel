// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){
        vec3 s  = tex2D_screen(c.xy).rgb;
        vec2 px = c.xy*umbriel_output_size;
        float d = length(px - umbriel_cursor);

        float t = d*0.07 + umbriel_time*1.0;
        vec3 tunnel = 0.5 + 0.5*cos(6.2831853*(t + vec3(0.0,0.33,0.67)));
        float fill  = barrulus_smoothstep(60.0, 18.0, d);
        return vec4(mix(s, tunnel, fill*0.09), 1.0);
    }
