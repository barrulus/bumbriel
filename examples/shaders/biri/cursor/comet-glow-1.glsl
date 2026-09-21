// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
vec4 postprocess(vec3 c){
            vec3 scr   = tex2D_source(c.xy).rgb;                            // original screen, unfiltered
            vec3 trail = tex2D_screen(c.xy).rgb;                           // pass 0's trail = rainbow hue * strength
            float i    = clamp(max(trail.r, max(trail.g, trail.b)), 0.0, 1.0); // trail strength 0..1
            vec3 hue   = trail / max(i, 1e-4);                             // recover the rainbow hue
            float lum  = dot(scr, vec3(0.299,0.587,0.114));                 // screen brightness 0..1
            vec3 glow  = scr + trail;                                       // additive glow  (great on dark)
            vec3 paint = mix(scr, hue, i);                                  // solid paint    (visible on light)
            return vec4(mix(glow, paint, lum), 1.0);                        // dark bg -> glow, light bg -> paint
        }
