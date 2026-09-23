// Shader by Barrulus, adapted for Umbriel.
vec4 postprocess(vec3 c){ vec3 s=tex2D_screen(c.xy).rgb; return vec4(s*vec3(1.05,0.92,0.78), 1.0); }
