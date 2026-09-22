// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
vec4 postprocess(vec3 c){
            vec3 scr   = tex2D_source(c.xy).rgb;
            vec3 trail = tex2D_screen(c.xy).rgb;
            float i    = clamp(max(trail.r, max(trail.g, trail.b)), 0.0, 1.0);
            vec3 hue   = trail / max(i, 1e-4);
            float lum  = dot(scr, vec3(0.299,0.587,0.114));
            vec3 glow  = scr + trail;
            vec3 paint = mix(scr, hue, i);
            return vec4(mix(glow, paint, lum), 1.0);
        }
