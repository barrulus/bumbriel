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
    for (int i = 0; i < 3; i++){
        v  += amp * vnoise(p);
        p   = p * 2.03 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return v;
}

vec3 rainbow(float h){
    // A zero count means the palette is off, which is how a shader keeps its own colours.
    if (umbriel_palette_count > 0) return umbriel_palette_at(h).rgb;
    return 0.5 + 0.5 * cos(6.2831853 * (h + vec3(0.0, 0.33, 0.67)));
}

vec4 postprocess(vec3 c){
    vec4  s  = tex2D_screen(c.xy);
    float ar = umbriel_size.x / max(umbriel_size.y, 1.0);
    float t  = umbriel_time;

    const float BANDS    = 1.6;
    const float SPEED    = 0.22;
    const float STRENGTH = 0.42;
    const float WOBBLE   = 0.10;
    const float WARP     = 0.12;
    const float TURN     = 0.06;
    const float CROSS    = 0.25;
    const float FOAM     = 0.25;
    const float SAT      = 0.90;

    vec2  a    = vec2(c.x * ar, c.y);
    float gate = barrulus_smoothstep(0.0, 0.25, s.a);

    float th  = 0.45 + 0.6 * sin(t * TURN);
    vec2  dir = vec2(cos(th), sin(th));
    vec2  prp = vec2(-dir.y, dir.x);
    float u   = dot(a, dir);
    float v   = dot(a, prp);

    u += sin(v * 4.0 + t * 0.7) * WOBBLE;
    u += (fbm(a * 1.8 + vec2(t * 0.06, -t * 0.04)) - 0.5) * WARP * 2.0;

    float ph  = u * BANDS - t * SPEED;
    float hue = fract(ph);
    vec3  col = rainbow(hue);
    col = mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, SAT);

    float crest = 0.5 + 0.5 * cos(6.2831853 * ph);
    col *= 0.80 + 0.30 * crest;
    col += vec3(0.90, 0.95, 1.0) * pow(crest, 12.0) * FOAM;

    vec2  dir2 = vec2(cos(th + 1.9), sin(th + 1.9));
    float v2   = dot(a, vec2(-dir2.y, dir2.x));
    float u2   = dot(a, dir2) + sin(v2 * 3.0 - t * 0.5) * WOBBLE;
    vec3  col2 = rainbow(fract(u2 * BANDS * 0.7 + t * SPEED * 0.6));
    col = mix(col, col2, CROSS);

    float swell = 0.80 + 0.20 * sin(t * 0.5 + u * 2.0);
    float cov   = STRENGTH * swell * gate;

    return vec4(mix(s.rgb, col, cov), mix(s.a, 1.0, cov));
}
