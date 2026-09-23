// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float hash11(float n){ return fract(sin(n*127.1)      * 43758.5453); }
vec2  hash21(float n){ return fract(sin(vec2(n*127.1, n*311.7)) * 43758.5453); }

vec4 postprocess(vec3 c){
    vec2 uv = c.xy;
    vec2 ar = vec2(umbriel_size.x / max(umbriel_size.y, 1.0), 1.0);

    const int   N    = 3;
    const float DUTY = 0.5;
    vec2  disp = vec2(0.0);
    float hi   = 0.0;

    for (int i = 0; i < N; i++) {
        float fi     = float(i);
        float period = 3.0 + hash11(fi + 0.3) * 3.5;
        float t      = umbriel_time / period + hash11(fi + 5.7) * 7.0;
        float life   = fract(t);
        vec2  origin = hash21(floor(t)*3.19 + fi*11.0);

        float p    = life / DUTY;
        vec2  delta = uv - origin;
        float d    = length(delta * ar);
        float r    = p * 0.55;
        float env  = barrulus_smoothstep(0.0, 0.08, p) * barrulus_smoothstep(1.0, 0.5, p);
        float wave = sin((d - r)*90.0) * exp(-abs(d - r)*22.0) * env;

        vec2 dir = delta / max(length(delta), 1e-4);
        disp += dir * wave * 0.010;
        hi   += max(wave, 0.0);
    }

    vec4 s = tex2D_screen(uv + disp);
    return vec4(s.rgb + hi*0.12, s.a);
}
