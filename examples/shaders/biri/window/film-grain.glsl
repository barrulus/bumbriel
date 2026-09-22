// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

const float GRAIN_INTERVAL = 15.0;
const float GRAIN_OPACITY  = 0.035;
const float GRAIN_SIZE     = 1.0;

vec4 postprocess(vec3 c){
    vec4 s = tex2D_screen(c.xy);

    vec2 offset = fract(vec2(floor(umbriel_time * GRAIN_INTERVAL)) * 0.36593);

    vec2 q    = floor(c.xy * umbriel_size / GRAIN_SIZE);
    vec2 seed = fract(q * 0.01371 + offset);
    float g   = (fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 2.0;

    float luma = dot(s.rgb, vec3(0.2126, 0.7152, 0.0722));
    float ext  = abs(luma * 2.0 - 1.0);
    float gain = 1.0 + biri_smoothstep(0.85, 0.95, ext) * 1.10;

    return vec4(s.rgb + g * GRAIN_OPACITY * gain * s.a, s.a);
}
