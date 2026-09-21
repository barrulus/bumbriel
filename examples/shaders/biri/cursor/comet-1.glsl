// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
vec4 postprocess(vec3 c){
            vec3 scr   = tex2D_source(c.xy).rgb;                            // original screen, unfiltered
            vec3 trail = tex2D_screen(c.xy).rgb;                           // pass 0's trail = rainbow hue * strength
            float i    = clamp(max(trail.r, max(trail.g, trail.b)), 0.0, 1.0); // trail strength 0..1
            vec3 hue   = trail / max(i, 1e-4);                             // recover the rainbow hue
            // Paint the rainbow OVER the screen (replace, not add) so it's visible on ANY background
            // — white included — and never over-brightens the whole desktop. Opaque (1.0).
            return vec4(mix(scr, hue, i), 1.0);
        }
