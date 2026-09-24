// Compact rainbow wax band with inward, rooted shedding.
const float BLEED_SPEED = 0.65;

float neon_hue(vec2 p) {
    vec2 q = (p - ring_size * 0.5) / max(ring_size * 0.5, vec2(1.0));
    float u = atan(q.y, q.x) / BLEED_TAU;
    float phase = umbriel_time / 22.0;
    return u - phase + 0.035 * sin(BLEED_TAU * (u * 3.0 + phase));
}

vec2 bleed_edges(vec2 p) {
    float wave = bleed_edge_wave(p);
    return vec2(-3.2 - 1.8 * wave, min(ring_width, 10.0));
}

vec3 bleed_pigment(float along, float depth, float seed, float sheen) {
    float perimeter = 2.0 * (ring_size.x + ring_size.y);
    vec2 p;
    if (along < ring_size.x) p = vec2(along, 0.0);
    else if (along < ring_size.x + ring_size.y) p = vec2(ring_size.x, along - ring_size.x);
    else if (along < 2.0 * ring_size.x + ring_size.y)
        p = vec2(2.0 * ring_size.x + ring_size.y - along, ring_size.y);
    else p = vec2(0.0, perimeter - along);
    float hue = neon_hue(p);
    vec3 rgb = clamp(abs(fract(hue + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return mix(mix(rgb, vec3(1.0), 0.20), vec3(1.0), sheen * 0.70);
}
