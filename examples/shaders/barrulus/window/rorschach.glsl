// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    float a = hash(i), b = hash(i + vec2(1.0,0.0));
    float d = hash(i + vec2(0.0,1.0)), e = hash(i + vec2(1.0,1.0));
    return mix(mix(a,b,f.x), mix(d,e,f.x), f.y);
}

float fbm(vec2 p){
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++){ v += a*vnoise(p); p *= 2.0; a *= 0.5; }
    return v;
}

const float MORPH = 0.55;

float blot(vec2 q){
    float t = umbriel_time * MORPH;
    vec2 w = vec2(fbm(q + vec2(0.0, t*0.11) + 3.7),
                  fbm(q + vec2(t*0.09, 0.0) + 1.3));
    return fbm(q + 2.2*w + vec2(0.0, t*0.03));
}

vec4 postprocess(vec3 c){
    vec4 s  = tex2D_screen(c.xy);
    vec2 ar = vec2(umbriel_size.x / max(umbriel_size.y, 1.0), 1.0);

    const float OPACITY = 0.55;
    const float SPREAD  = 0.35;
    const float SCALE   = 3.0;
    const float WOBBLE  = 1.1;
    const float EDGE    = 0.012;

    vec2  p = (c.xy - 0.5) * ar;
    vec2  q = vec2(abs(p.x), p.y) * SCALE;
    float r = length(p);

    float n      = (blot(q) - 0.47) + 0.10*(vnoise(q*9.0 + umbriel_time*0.2) - 0.5);
    float breath = 0.05*sin(umbriel_time*0.21);
    float radius = SPREAD * max(0.55 + breath + WOBBLE*n, 0.15);

    float ink = barrulus_smoothstep(radius + EDGE, radius - EDGE, r);

    vec3 black = vec3(0.03, 0.03, 0.04);
    return vec4(mix(s.rgb, black, ink * OPACITY), s.a);
}
