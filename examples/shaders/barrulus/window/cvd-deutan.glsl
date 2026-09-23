// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float grain(vec2 px, float cellsz, float hz, float ofs){
    vec2 t    = fract(vec2(floor(umbriel_time * hz)) * 0.36593 + ofs);
    vec2 q    = floor(px / cellsz);
    vec2 seed = fract(q * 0.01371 + t);
    return (fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 2.0;
}

vec4 postprocess(vec3 c){
    vec4 s = tex2D_screen(c.xy);

    vec3 sim = vec3(dot(vec3(0.625, 0.375, 0.0), s.rgb),
                    dot(vec3(0.700, 0.300, 0.0), s.rgb),
                    dot(vec3(0.0,   0.300, 0.7), s.rgb));

    vec3  err  = s.rgb - sim;
    float loss = length(err);
    float amp  = barrulus_smoothstep(0.06, 0.45, loss) * s.a;
    if (amp < 0.003) return s;

    float side    = err.r - err.g;
    float sideMix = barrulus_smoothstep(-0.05, 0.05, side);

    vec2  px = c.xy * umbriel_size;
    float gR = grain(px, 3.0,  8.0, 0.0);
    float gG = grain(px, 1.0, 20.0, 0.37);
    float g  = mix(gG, gR, sideMix);

    const float STRENGTH = 0.22;
    return vec4(s.rgb + g * amp * STRENGTH, s.a);
}
