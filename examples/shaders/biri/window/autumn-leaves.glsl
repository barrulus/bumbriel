// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash1(float n){ return hash(vec2(n, 1.7)); }
vec2 rot(vec2 p, float a){ float sn = sin(a); float cs = cos(a); return vec2(p.x * cs - p.y * sn, p.x * sn + p.y * cs); }

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const int   LEAVES   = 18;
    const float OPACITY  = 0.92;
    const float SPEED    = 0.12;
    const float MAX_RISE = 0.5;
    const float SIZE     = 0.020;

    if (c.y < 1.0 - MAX_RISE - 0.07) return s;

    vec2  a    = vec2(c.x * ar, c.y);
    float gate = biri_smoothstep(0.0, 0.25, s.a);

    vec3  rgb   = s.rgb;
    float alpha = s.a;

    for (int i = 0; i < LEAVES; i++){
        float fi = float(i);
        float r1 = hash1(fi * 1.61 + 0.7);
        float r2 = hash1(fi * 2.23 + 4.1);
        float r3 = hash1(fi * 3.71 + 8.9);
        float r4 = hash1(fi * 5.13 + 2.3);

        float spd = SPEED * (0.6 + 0.9 * r2);
        float x   = fract(r1 * 7.31 + t * spd + 0.05 * sin(t * 0.7 + r1 * 6.28));
        float X   = x * (ar + 0.24) - 0.12;

        float lowH  = 0.05 + 0.16 * r3 * r3;
        float highH = 0.22 + (MAX_RISE - 0.24) * r3;
        float hgt   = mix(lowH, highH, step(0.8, r4));
        float Y     = 1.0 - hgt
                    + 0.030 * sin(t * (0.9 + r2) + r1 * 6.28)
                    + 0.015 * sin(t * 2.6 + r4 * 6.28);

        vec2  p  = a - vec2(X, Y);
        float sz = SIZE * (0.65 + 0.7 * r2);
        if (dot(p, p) > sz * sz * 12.0) continue;

        float ang    = t * (1.2 + 2.4 * r2) * sign(r4 - 0.5) + r1 * 6.28;
        float squash = 0.30 + 0.70 * abs(sin(t * (0.8 + 1.6 * r3) + r2 * 6.28));
        vec2  q      = rot(p, ang);
        float d      = length(vec2(q.x, q.y * 1.8 / squash)) / sz;
        float m      = biri_smoothstep(1.0, 0.80, d) * gate;

        vec3 lc = mix(vec3(0.72, 0.30, 0.10), vec3(0.87, 0.60, 0.16), biri_smoothstep(0.25, 0.60, r3));
        lc      = mix(lc, vec3(0.46, 0.44, 0.14), biri_smoothstep(0.70, 0.90, r1));
        lc     *= (0.80 + 0.30 * r2) * (0.62 + 0.38 * squash);
        float rib = biri_smoothstep(0.14, 0.03, abs(q.y) / max(sz * squash, 0.0001));
        lc      = mix(lc, lc * 0.72, rib);

        float cov = m * OPACITY;
        rgb   = mix(rgb, lc, cov);
        alpha = mix(alpha, 1.0, cov);
    }

    return vec4(rgb, alpha);
}
