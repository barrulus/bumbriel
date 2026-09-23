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

vec3 pal(float x){
    vec3 col = mix(vec3(0.28, 0.38, 0.20), vec3(0.78, 0.64, 0.28), barrulus_smoothstep(0.15, 0.55, x));
    return mix(col, vec3(0.93, 0.88, 0.66), barrulus_smoothstep(0.60, 0.92, x));
}

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const float SWEEP    = 0.28;
    const float STRENGTH = 0.35;
    const float SHEEN    = 0.12;
    const float SHADE    = 0.07;
    const float SCALE    = 1.0;
    const float WANDER   = 1.4;
    const vec2  WIND     = vec2(0.966, 0.259);

    vec2  a    = vec2(c.x * ar, c.y);
    float gate = barrulus_smoothstep(0.0, 0.25, s.a);

    vec2  perp0 = vec2(-WIND.y, WIND.x);
    float wob   = ((fbm(a * 0.9 + vec2(t * 0.09, 0.0)) - 0.5) * WANDER + 0.25 * sin(t * 0.40));
    vec2  wdir  = normalize(WIND + perp0 * wob);
    vec2  wperp = vec2(-wdir.y, wdir.x);
    float u = dot(a, wdir);
    float v = dot(a, wperp);

    vec2 gp = vec2(u * 1.4 * SCALE - t * SWEEP * 2.2, v * 1.9 * SCALE);
    gp += (vec2(fbm(gp * 0.9 + 3.7), fbm(gp * 0.9 + 8.1)) - 0.5) * 0.9;
    float g  = fbm(gp);
    float g2 = fbm(gp + vec2(0.14, 0.0));

    float surge = 0.70 + 0.30 * vnoise(vec2(t * 0.35, 3.3));
    float gust  = barrulus_smoothstep(0.32, 0.70, g) * surge;
    float paw   = fbm(a * 2.4 * SCALE + vec2(-t * SWEEP * 1.2, t * 0.10));
    float burst = barrulus_smoothstep(0.50, 0.76, paw) * (0.55 + 0.45 * sin(t * 2.8 + paw * 12.0));
    gust = clamp(gust + burst * 0.7, 0.0, 1.0);

    float rip = (vnoise(vec2(u * 9.0 - t * SWEEP * 7.0, v * 14.0)) - 0.5) * gust;

    float push = t * SWEEP * 1.6 + gust * 0.35 + rip * 0.15;
    float f    = fbm(vec2((u - push) * 2.2, v * 3.6 - t * 0.22));
    vec3  wc   = pal(clamp(f * 1.5 - 0.15, 0.0, 1.0));

    float amt = barrulus_smoothstep(0.10, 0.65, gust) * STRENGTH * gate;
    vec3  rgb = mix(s.rgb, wc, amt);

    float front = clamp((g - g2) * 3.0, 0.0, 1.0) * gust;
    rgb += vec3(0.90, 0.86, 0.68) * front * SHEEN * s.a * gate;
    rgb *= 1.0 - SHADE * gust * (1.0 - front) * gate;
    rgb += vec3(0.88, 0.80, 0.52) * max(rip, 0.0) * 0.12 * s.a * gate;

    return vec4(rgb, s.a);
}
