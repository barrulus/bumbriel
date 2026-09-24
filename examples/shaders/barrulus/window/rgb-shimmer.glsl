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
    for (int i = 0; i < 3; i++){ v += a*vnoise(p); p *= 2.0; a *= 0.5; }
    return v;
}

vec4 postprocess(vec3 c){
    vec4 s  = tex2D_screen(c.xy);
    vec2 ar = vec2(umbriel_size.x / max(umbriel_size.y, 1.0), 1.0);
    vec2 p  = c.xy * ar;

    float f   = fbm(p*3.0 + vec2(umbriel_time*0.13, umbriel_time*0.08));
    float hue = fract(f + umbriel_time*0.08);
    // A zero count means the palette is off, which is how a shader keeps its own colours.
    vec3 rb = umbriel_palette_count > 0
        ? umbriel_palette_at(hue).rgb
        : 0.5 + 0.5*cos(6.2831853*(hue + vec3(0.0, 0.33, 0.67)));

    float patch = barrulus_smoothstep(0.35, 0.62, fbm(p*2.0 - vec2(umbriel_time*0.10, 0.0)));

    float spark = pow(vnoise(p*38.0 + umbriel_time*1.5), 22.0) * 0.35;

    const float STRENGTH = 0.38;
    float amt = patch * STRENGTH;
    return vec4(mix(s.rgb, rb + spark, amt), s.a);
}
