// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}
vec4 postprocess(vec3 c){ vec3 s=tex2D_screen(c.xy).rgb; float v=barrulus_smoothstep(0.85,0.35,length(c.xy-0.5)); return vec4(s*mix(0.5,1.0,v), 1.0); }
