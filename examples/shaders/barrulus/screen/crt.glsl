// Shader by Barrulus, adapted for Umbriel.
vec4 postprocess(vec3 c){ vec3 s=tex2D_screen(c.xy).rgb; float scan=0.85+0.15*sin(c.y*umbriel_size.y*3.14159); return vec4(s*scan, 1.0); }
