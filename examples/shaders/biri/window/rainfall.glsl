// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2 hash2(vec2 p){ return vec2(hash(p), hash(p + 19.19)); }

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const float REFRACT = 1.0;
    const float RAIN    = 0.55;
    const float FOG     = 0.35;
    const float TINT    = 0.12;
    const float BUILDUP = 1.0;

    vec2  a    = vec2(c.x * ar, c.y);
    float gate = biri_smoothstep(0.0, 0.25, s.a);

    vec2  off = vec2(0.0);
    float wet = 0.0;

    float rain = 0.0;
    for (int i = 0; i < 3; i++){
        float fi    = float(i);
        float slant = 0.06 + fi * 0.04;
        float px    = a.x + a.y * slant + fi * 0.37;
        float colW  = 0.024 + fi * 0.014;
        float ci    = floor(px / colW);
        float rn    = hash(vec2(ci, 17.0 + fi * 5.0));
        float vel   = (1.1 - fi * 0.20) * (0.8 + 0.4 * hash(vec2(ci, 23.0 + fi)));
        float xc    = (ci + 0.5) * colW + (rn - 0.5) * colW * 0.5;
        float soft  = biri_smoothstep(colW * 0.20, 0.0, abs(px - xc));
        float v     = fract((a.y - t * vel) / 0.6 + rn * 13.0);
        float lenN  = 0.22 + 0.25 * rn;
        float seg   = biri_smoothstep(0.0, lenN, v) * (1.0 - biri_smoothstep(lenN, lenN + 0.10, v));
        rain += soft * seg * (1.0 - fi * 0.28);
    }
    rain = min(rain, 1.0);

    for (int i = 0; i < 2; i++){
        float fi   = float(i);
        float colW = 0.16 - fi * 0.06;
        float ci   = floor(a.x / colW);
        float rnd  = hash(vec2(ci, 3.7 + fi * 11.0));
        float spd  = 0.10 + 0.18 * hash(vec2(ci, 9.3 + fi));
        float dy   = fract(rnd * 13.7 + t * spd);

        float bx   = (ci + 0.5) * colW + (rnd - 0.5) * colW * 0.4;
        float wigA = colW * 0.18;
        float px   = bx + sin(a.y * 18.0 + rnd * 6.28) * wigA;

        float r    = (0.011 - fi * 0.003) * (0.8 + 0.4 * rnd);
        vec2  d    = vec2(a.x - px, (a.y - dy) * 0.85);
        float drop = biri_smoothstep(r, r * 0.5, length(d));
        off       -= (d / max(r, 0.0001)) * drop * 0.020 * REFRACT;

        float rowY = (floor(a.y * 70.0) + 0.5) / 70.0;
        float pxb  = bx + sin(rowY * 18.0 + rnd * 6.28) * wigA;
        float fade = biri_smoothstep(dy - 0.30, dy, a.y) * step(a.y, dy);
        float rb   = r * (0.25 + 0.45 * hash(vec2(ci, rowY * 91.0))) * fade;
        vec2  db   = vec2(a.x - pxb, (a.y - rowY) * 1.2);
        float bead = biri_smoothstep(rb, rb * 0.4, length(db)) * step(0.001, rb);
        off       -= (db / max(rb, 0.0001)) * bead * 0.006 * REFRACT;

        wet = max(wet, max(drop, bead * 0.8));
    }

    for (int i = 0; i < 2; i++){
        float fi = float(i);
        float sc = 22.0 + fi * 16.0;
        vec2  g  = a * sc + fi * 13.1;
        vec2  id = floor(g);
        vec2  f  = fract(g) - 0.5;
        float on = step(1.0 - 0.5 * BUILDUP, hash(id + 1.3));
        vec2  p  = (hash2(id + 7.7) - 0.5) * 0.6;
        float rn = hash(id + 4.4);
        float life = fract(rn + t * 0.02);
        float sz   = biri_smoothstep(0.0, 0.35, life) * biri_smoothstep(1.0, 0.8, life);
        float rad  = (0.10 + 0.22 * rn) * sz * on;
        vec2  d2   = f - p;
        float m    = biri_smoothstep(rad, rad * 0.55, length(d2)) * step(0.001, rad);
        off       -= (d2 / max(rad, 0.0001)) * m * 0.010 * REFRACT;
        wet        = max(wet, m * 0.7);
    }

    off *= gate;
    wet *= gate;

    vec2 uvR = clamp(c.xy + vec2(off.x / max(ar, 0.001), off.y), 0.0, 1.0);
    vec3 rgb = tex2D_screen(uvR).rgb;

    vec2 e = 1.5 / max(umbriel_size, vec2(1.0));
    vec3 blur = ( tex2D_screen(clamp(c.xy + vec2(e.x, 0.0), 0.0, 1.0)).rgb
                + tex2D_screen(clamp(c.xy - vec2(e.x, 0.0), 0.0, 1.0)).rgb
                + tex2D_screen(clamp(c.xy + vec2(0.0, e.y), 0.0, 1.0)).rgb
                + tex2D_screen(clamp(c.xy - vec2(0.0, e.y), 0.0, 1.0)).rgb ) * 0.25;
    rgb = mix(rgb, blur, FOG * (1.0 - min(wet * 1.6, 1.0)) * gate);

    rgb += vec3(0.62, 0.70, 0.84) * rain * RAIN * 0.24 * (1.0 - wet * 0.85) * gate * s.a;

    float lum = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(rgb, vec3(lum) * vec3(0.82, 0.90, 1.06), TINT * gate);
    rgb += 0.05 * wet * s.a;

    return vec4(rgb, s.a);
}
