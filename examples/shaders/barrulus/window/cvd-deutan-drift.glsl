// Shader by Barrulus, adapted for Umbriel.
// Descending smoothstep edges are undefined in GLSL.
float barrulus_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

float grainCell(vec2 q){
    vec2 seed = fract(q * 0.01371);
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

    float axis = clamp(err.r - err.g, -1.0, 1.0);
    float phi  = axis * 1.571;
    vec2  dir  = vec2(cos(phi), -sin(phi));

    const float SPEED = 30.0;
    vec2  px = c.xy * umbriel_size - dir * umbriel_time * SPEED;
    float g  = grainCell(floor(px / 2.0));

    const float STRENGTH = 0.22;
    return vec4(s.rgb + g * amp * STRENGTH, s.a);
}
