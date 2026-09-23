// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
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
    for (int i = 0; i < 4; i++){
        v  += amp * vnoise(p);
        p   = p * 2.03 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return v;
}

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const float DENSITY = 0.50;
    const float BOIL    = 0.75;
    const float CHURN   = 0.38;
    const float RISE    = 0.09;
    const float OPACITY = 0.40;

    vec2  a    = vec2(c.x * ar, c.y);
    float gate = barrulus_smoothstep(0.0, 0.25, s.a);

    vec2  rel = a - vec2(ar * 0.5, 0.45);
    float r   = length(rel);
    vec2  p   = rel * 2.6;

    vec2 q = vec2(fbm(p * 0.8 + vec2(0.0,  t * CHURN)),
                  fbm(p * 0.8 + vec2(5.2, -t * CHURN * 0.85)));
    vec2  wp = p + (q - 0.5) * BOIL * 2.8 + vec2(0.0, t * RISE);
    float n  = fbm(wp);

    float dens = n * (0.45 + DENSITY) * (1.05 - 0.20 * barrulus_smoothstep(0.1, 0.9, r));
    float body = barrulus_smoothstep(0.40, 0.68, dens);
    float wisp = barrulus_smoothstep(0.28, 0.48, dens);

    float n2  = fbm(wp + vec2(0.0, -0.35));
    float lit = clamp(0.62 + (n - n2) * 2.2, 0.30, 1.0);

    vec3  cl  = mix(vec3(0.30, 0.32, 0.37), vec3(0.66, 0.68, 0.72), lit);
    float cov = clamp(body + wisp * 0.35, 0.0, 1.0) * OPACITY * gate;

    return vec4(mix(s.rgb, cl, cov), mix(s.a, 1.0, cov));
}
