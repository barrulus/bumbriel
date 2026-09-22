// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2 hash2(vec2 p){ return vec2(hash(p), hash(p + 19.19)); }

vec4 postprocess(vec3 c){
    vec4 s  = tex2D_screen(c.xy);
    vec2 ar = vec2(umbriel_size.x / max(umbriel_size.y, 1.0), 1.0);
    float snow = 0.0;

    for (int i = 0; i < 3; i++){
        float fi    = float(i);
        float scale = 14.0 + fi * 10.0;
        float speed = 0.055 - fi * 0.015;

        vec2 p = c.xy * ar;
        p.x += sin(umbriel_time * 0.35 + fi * 2.1 + c.y * 4.0) * 0.012;
        p.y -= umbriel_time * speed;

        vec2  g    = p * scale;
        vec2  cell = floor(g);
        vec2  f    = fract(g);
        float on   = step(0.72, hash(cell + fi * 17.0));
        vec2  fp   = 0.2 + 0.6 * hash2(cell + fi * 31.0);
        float d    = length(f - fp);
        float r    = 0.10 + 0.12 * hash(cell + 3.7);
        float flake = biri_smoothstep(r, r * 0.15, d);

        snow += flake * on * (1.0 - fi * 0.3);
    }

    const float OPACITY = 0.35;
    float m = min(snow, 1.0) * OPACITY * s.a;
    return vec4(mix(s.rgb, vec3(0.95, 0.97, 1.0), m), s.a);
}
