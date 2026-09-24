// Slow violet and orchid pools with sparse, erratic magical filaments.
const float BLEED_SPEED = 0.58;

vec3 bleed_pigment(float along, float depth, float seed, float sheen) {
    float perimeter = 2.0 * (ring_size.x + ring_size.y);
    float u = along / perimeter;
    float t = umbriel_time * BLEED_SPEED;
    float turns = max(floor(perimeter / 210.0), 1.0);
    float phase = u * BLEED_TAU * turns;
    float swirl = sin(phase - t * 0.8 + depth * 0.046
        + 1.3 * sin(phase * 2.0 + t * 0.45 - depth * 0.035));
    float pools = sin(phase + t * 0.55 + depth * 0.065 + 1.2 * swirl);
    vec3 violet = vec3(0.42, 0.035, 0.88);
    vec3 orchid = vec3(0.78, 0.14, 0.98);
    vec3 amethyst = vec3(0.56, 0.27, 1.0);
    vec3 pigment = mix(violet, orchid, smoothstep(-0.65, 0.75, swirl));
    pigment = mix(pigment, amethyst, 0.38 * (0.5 + 0.5 * pools));
    // Fine, broken contour lines fizz faster than the underlying liquid moves.
    float fizz = umbriel_time * 1.25;
    float filament_field = sin(phase * 3.0 + depth * 0.21 + 1.6 * swirl
        + 0.28 * sin(phase * 11.0 - fizz + depth * 0.36));
    float filament = 1.0 - smoothstep(0.025, 0.095, abs(filament_field));
    float sparks = smoothstep(0.52, 0.88,
        sin(phase * 5.0 + depth * 0.09 + sin(phase * 9.0 + fizz)));
    pigment = mix(pigment, vec3(0.62, 0.95, 0.24), filament * sparks * 0.72);
    float seam = (1.0 - smoothstep(0.025, 0.085, abs(filament_field + 0.36)))
        * smoothstep(0.45, 0.90, pools);
    pigment = mix(pigment, vec3(0.14, 0.025, 0.27), seam * 0.40);
    return mix(pigment, vec3(0.87, 0.65, 1.0), sheen * 0.48);
}

vec2 bleed_edges(vec2 p) {
    float wave = bleed_edge_wave(p);
    return vec2(-3.6 - 2.0 * wave, min(ring_width, 10.0));
}
