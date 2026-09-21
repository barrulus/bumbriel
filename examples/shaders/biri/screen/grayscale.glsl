// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
vec4 postprocess(vec3 c){ float g=dot(tex2D_screen(c.xy).rgb, vec3(0.299,0.587,0.114)); return vec4(vec3(g), 1.0); }
