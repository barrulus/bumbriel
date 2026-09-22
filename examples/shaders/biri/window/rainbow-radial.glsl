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
    float v   = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 3; i++){
        v  += amp * vnoise(p);
        p   = p * 2.03 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return v;
}

vec3 rainbow(float h){ return 0.5 + 0.5 * cos(6.2831853 * (h + vec3(0.0, 0.33, 0.67))); }

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const float RINGS    = 2.5;
    const float SPEED    = 0.18;
    const float STRENGTH = 0.42;
    const float WARP     = 0.10;
    const float WANDER   = 0.18;
    const float SWIRL    = 0.0;
    const float SAT      = 0.90;
    const float CORE     = 0.45;

    vec2  a    = vec2(c.x * ar, c.y);
    float gate = biri_smoothstep(0.0, 0.25, s.a);

    vec2  ctr = vec2(ar * 0.5, 0.5) + WANDER * vec2(sin(t * 0.21), 0.7 * cos(t * 0.17));
    vec2  rel = a - ctr;
    float r   = length(rel);
    float ang = atan(rel.y, rel.x + 0.00001);

    float w  = (fbm(a * 2.0 + vec2(t * 0.05, -t * 0.04)) - 0.5) * WARP;
    float rw = r + w;

    float ph  = rw * RINGS - t * SPEED + ang * SWIRL / 6.2831853;
    float hue = fract(ph);
    vec3  col = rainbow(hue);
    col = mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, SAT);

    float crest = 0.5 + 0.5 * cos(6.2831853 * ph);
    col *= 0.82 + 0.28 * crest;

    float fall    = 1.0 - 0.35 * biri_smoothstep(0.0, 1.0, r);
    float breathe = 0.86 + 0.14 * sin(t * 0.9);
    float shim    = 0.92 + 0.08 * sin(ang * 6.0 + t * 1.3 + rw * 8.0);
    float cov     = STRENGTH * fall * breathe * shim * gate;

    float core = exp(-r * r * 40.0) * CORE * (0.7 + 0.3 * sin(t * 2.0));
    col += vec3(1.0) * core;

    return vec4(mix(s.rgb, col, cov), mix(s.a, 1.0, cov));
}
