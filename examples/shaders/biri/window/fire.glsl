// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p){
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++){
        v += a * vnoise(p);
        p  = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const float FLAME_HEIGHT = 0.50;
    const float OPACITY      = 0.80;
    const float GLOW         = 0.55;
    const float RISE         = 1.5;
    const float DETAIL       = 9.0;
    const float SWAY         = 0.55;

    float h = 1.0 - c.y;
    float x = c.x * ar;

    if (h > FLAME_HEIGHT * 1.6) return s;

    float hn = h / FLAME_HEIGHT;

    float sway = (vnoise(vec2(x * 3.0, h * 2.5 - t * RISE * 0.5)) - 0.5) * SWAY * hn;

    vec2  q = vec2(x * DETAIL + sway * 3.0, h * 1.9 - t * RISE);
    float n = fbm(q) * 0.72 + fbm(q * 2.6 + vec2(0.0, -t * RISE * 1.9)) * 0.28;

    n *= 0.94 + 0.06 * sin(t * 11.0 + x * 6.0);

    float edge  = 0.10 + hn * 0.56 - biri_smoothstep(0.04, 0.0, h) * 0.08;
    float core  = biri_smoothstep(edge, edge + 0.22, n);
    float halo  = biri_smoothstep(edge - 0.20, edge + 0.34, n) * (1.0 - hn * 0.5);

    float temp = core * (1.0 - biri_smoothstep(0.0, 1.0, hn) * 0.62);
    vec3  col  = mix(vec3(0.62, 0.05, 0.01), vec3(1.0, 0.34, 0.02), biri_smoothstep(0.02, 0.40, temp));
    col        = mix(col, vec3(1.0, 0.74, 0.12),  biri_smoothstep(0.38, 0.70, temp));
    col        = mix(col, vec3(1.0, 0.94, 0.72),  biri_smoothstep(0.78, 1.0,  temp));

    vec3 rgb = s.rgb + vec3(1.0, 0.42, 0.08) * halo * GLOW * s.a;
    rgb      = mix(rgb, col, core * OPACITY * s.a);

    return vec4(rgb, s.a);
}
