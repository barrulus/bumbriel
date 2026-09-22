// Ported from Barrulus's live Biri window collection; GPL-3.0-only, see ../LICENSE.
// Descending smoothstep edges are undefined in GLSL.
float biri_smoothstep(float a, float b, float x) {
    return a > b ? 1.0 - smoothstep(b, a, x) : smoothstep(a, b, x);
}

const float RADIUS = 10.0;
const float GAIN   = 1.9;
const float DIM    = 0.55;
const float DIMLO  = 0.25;
const float DIMHI  = 0.60;

float lum(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }

vec4 postprocess(vec3 c){
    vec4 s  = tex2D_screen(c.xy);
    vec2 px = 1.0 / max(umbriel_size, vec2(1.0));

    vec3 m = s.rgb;
    for (int i = 0; i < 8; i++){
        float a = 0.7853982 * float(i);
        vec2  d = vec2(cos(a), sin(a)) * px;
        m += tex2D_screen(c.xy + d * RADIUS).rgb;
        m += tex2D_screen(c.xy + d * (RADIUS * 0.5)).rgb;
    }
    m /= 17.0;

    float dimf = mix(1.0, DIM, biri_smoothstep(DIMLO, DIMHI, lum(m)));

    vec3 outc = m * dimf + (s.rgb - m) * GAIN;
    return vec4(clamp(outc, 0.0, 1.0), s.a);
}
