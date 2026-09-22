// Ported from Barrulus/biri; GPL-3.0-only, see ../LICENSE.
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
    for (int i = 0; i < 3; i++){ v += a*vnoise(p); p *= 2.0; a *= 0.5; }
    return v;
}

float flow(vec2 q){
    vec2 w = vec2(fbm(q + vec2(0.0, umbriel_time*0.06)),
                  fbm(q + vec2(umbriel_time*0.05, 0.0)));
    return fbm(q + 1.6*w);
}

vec4 postprocess(vec3 c){
    vec4 s  = tex2D_screen(c.xy);
    vec2 ar = vec2(umbriel_size.x / max(umbriel_size.y, 1.0), 1.0);

    const float SCALE = 3.0;
    const float BUMP  = 2.2;
    const float SPECP = 50.0;

    vec2 q = c.xy * ar * SCALE;

    float e  = 0.06;
    float h0 = flow(q);
    float hx = flow(q + vec2(e, 0.0));
    float hy = flow(q + vec2(0.0, e));
    vec3  n  = normalize(vec3((h0 - hx)/e * BUMP, (h0 - hy)/e * BUMP, 1.0));

    vec3  r   = reflect(vec3(0.0, 0.0, -1.0), n);
    float env = 0.5 + 0.5*r.y;
    vec3  chrome = mix(vec3(0.20, 0.22, 0.27), vec3(0.90, 0.93, 1.0), env);

    vec3  L    = normalize(vec3(cos(umbriel_time*0.4), 0.4 + 0.4*sin(umbriel_time*0.3), 0.9));
    float spec = pow(max(dot(r, L), 0.0), SPECP) * 1.6;

    float lum    = dot(s.rgb, vec3(0.299, 0.587, 0.114));
    vec3  silver = mix(s.rgb, vec3(lum), 0.5);
    vec3  metal  = silver * mix(0.55, 1.35, env) + chrome*0.25 + spec;

    const float OPACITY = 0.55;
    return vec4(mix(s.rgb, metal, OPACITY), s.a);
}
