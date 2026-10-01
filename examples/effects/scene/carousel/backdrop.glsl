// Static Noctalia-coloured universe. No clock dependency or image allocation.
// All colours are RGB triples in [0,1]; configuration supplies the palette.
uniform float backdrop_style; // 0 off, 1 solid, 2 universe
uniform float backdrop_opacity;
uniform float backdrop_brightness;
uniform vec3 backdrop_color;
uniform vec3 nebula_color;
uniform vec3 nebula_accent;
uniform vec3 star_color;
uniform float nebula_strength;
uniform float star_density;

float sky_hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float sky_noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(sky_hash(cell), sky_hash(cell + vec2(1.0, 0.0)), f.x),
        mix(sky_hash(cell + vec2(0.0, 1.0)), sky_hash(cell + vec2(1.0)), f.x), f.y);
}
float sky_cloud(vec2 p) {
    float cloud = 0.0;
    float weight = 0.5;
    for (int i = 0; i < 5; i++) {
        cloud += weight * sky_noise(p);
        p = mat2(1.6, -1.2, 1.2, 1.6) * p + vec2(3.7, 8.1);
        weight *= 0.5;
    }
    return cloud;
}
float sky_stars(vec2 p, float grid, float seed, float chance) {
    vec2 cell = floor(p * grid);
    float pick = sky_hash(cell + seed);
    float enabled = step(1.0 - chance * clamp(star_density, 0.0, 3.0), pick);
    vec2 centre = vec2(sky_hash(cell + seed + 7.3), sky_hash(cell + seed + 19.1));
    vec2 delta = (fract(p * grid) - centre) * umbriel_output_size.y / grid;
    float distance = length(delta);
    float size = mix(0.65, 1.35, sky_hash(cell + seed + 31.0));
    float core = 1.0 - smoothstep(0.0, size, distance);
    float glow = exp(-distance * 0.7) * 0.18;
    return enabled * (core + glow) * mix(0.45, 1.0, sky_hash(cell + seed + 43.0));
}
vec4 transition_backdrop(vec2 uv) {
    if (backdrop_style < 0.5) return vec4(0.0);
    float opacity = clamp(backdrop_opacity, 0.0, 1.0)
        * smoothstep(0.0, 1.0, umbriel_clamped_progress);
    vec3 colour = clamp(backdrop_color, 0.0, 1.0);
    if (backdrop_style >= 1.5) {
        vec2 p = (uv - 0.5) * vec2(umbriel_output_size.x / umbriel_output_size.y, 1.0);
        vec2 q = mat2(0.91, -0.41, 0.41, 0.91) * p;
        float mist = sky_cloud(q * 3.2 + vec2(12.0, 4.0));
        float detail = sky_cloud(q * 8.0 + vec2(2.0, 17.0));
        float ribbon = exp(-pow((q.y + (mist - 0.5) * 0.5) * 3.4, 2.0));
        float clouds = ribbon * smoothstep(0.24, 0.78, mist) * (0.35 + 0.65 * detail);
        vec3 gas = mix(clamp(nebula_color, 0.0, 1.0), clamp(nebula_accent, 0.0, 1.0),
            smoothstep(-0.65, 0.8, q.x + mist * 0.5));
        colour = colour * 0.45 + gas * clouds * clamp(nebula_strength, 0.0, 3.0);
        colour *= 1.0 - 0.3 * smoothstep(0.2, 1.2, length(p));
        float stars = sky_stars(p + 2.0, 70.0, 1.7, 0.035)
            + sky_stars(p + 2.0, 140.0, 9.2, 0.014);
        colour += mix(vec3(1.0), clamp(star_color, 0.0, 1.0), 0.45) * stars;
    }
    colour = clamp(colour * clamp(backdrop_brightness, 0.0, 3.0), 0.0, 1.0);
    return vec4(colour * opacity, opacity);
}
