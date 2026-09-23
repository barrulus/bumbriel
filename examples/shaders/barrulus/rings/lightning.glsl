// Shader by Barrulus, adapted for Umbriel.
const int LIGHTNING_COUNT = 1; // 1-4, clamped
const float SPEED = 1.0;
const float STRENGTH = 0.9;
const float BRIGHTNESS = 1.0;
const float RING_OUTSET_MULTIPLIER = 3.5;

float fr_perimeter(vec2 coords, vec2 size) {
    vec2 half_size = max(size * 0.5, vec2(1.0));
    vec2 p = coords - half_size;
    vec2 q = p / max(max(abs(p.x) / half_size.x, abs(p.y) / half_size.y), 0.0001);
    float along;
    if (abs(q.y) / half_size.y >= abs(q.x) / half_size.x) {
        along = q.y < 0.0 ? q.x + half_size.x : size.x + size.y + half_size.x - q.x;
    } else {
        along = q.x > 0.0 ? size.x + q.y + half_size.y : 2.0 * size.x + size.y + half_size.y - q.y;
    }
    return along / (2.0 * (size.x + size.y));
}

float fr_hash(float p) {
    return fract(sin(p * 127.1 + 311.7) * 43758.5453);
}

float fr_crackle(float u, float cells, float tick) {
    float x = fract(u) * cells;
    float a = fr_hash(mod(floor(x), cells) + tick * 71.0);
    float b = fr_hash(mod(floor(x) + 1.0, cells) + tick * 71.0);
    return mix(a, b, fract(x)) * 2.0 - 1.0;
}

vec4 ring_color(vec2 coords) {
    if (ring_width <= 0.0 || min(ring_size.x, ring_size.y) <= 0.0) return vec4(0.0);
    const float tau = 6.28318530718;
    float phase = fract(umbriel_time * SPEED / 4.0);
    float strength = clamp(STRENGTH, 0.0, 1.0);
    float width = ring_width;
    vec2 inner_size = ring_size;
    vec2 local = coords;
    float distance = ring_distance(coords);
    float aa = 0.5 / max(umbriel_scale, 0.01);
    if (distance <= 0.0 || distance >= RING_OUTSET_MULTIPLIER * width + 2.0 * aa) return vec4(0.0);
    float u = fr_perimeter(local, inner_size);
    float d = distance / width;
    vec3 rgb;
    float coverage;

    float count = clamp(float(LIGHTNING_COUNT), 1.0, 4.0);
    float behind = fract((phase - u) * count) / count;
    float delta = abs(fract((u - phase) * count + 0.5) - 0.5) / count;
    float head = exp(-pow(delta / 0.027, 2.0));
    float wake = exp(-behind * 19.0);
    float energy = max(head, wake * 0.65);
    float tick = floor(phase * 96.0);
    float jag = fr_crackle(u, 180.0, tick)
        + 0.35 * fr_crackle(u, 420.0, tick + 13.0);
    float center = 0.65 + strength * (0.22 + 0.58 * energy * jag);
    float bolt_distance = abs(d - center);
    float core = 1.0 - smoothstep(0.06, 0.06 + aa / width, bolt_distance);
    float halo = exp(-bolt_distance * 5.5);
    float fork_center = center + strength * energy * (0.55 + 0.4 * fr_crackle(u, 90.0, tick + 7.0));
    float fork = exp(-abs(d - fork_center) * 18.0) * energy;
    float spark = exp(-pow(delta / 0.007, 2.0)) * exp(-abs(d - center) * 2.2);
    float light = core * (0.18 + 0.82 * energy) + 0.7 * fork + 0.85 * spark;
    rgb = mix(vec3(0.08, 0.24, 0.8), vec3(0.78, 0.94, 1.0), clamp(light, 0.0, 1.0));
    coverage = clamp(light + halo * (0.12 + 0.48 * energy), 0.0, 1.0);

    float outer_limit = min(RING_OUTSET_MULTIPLIER * width, width + ring_padding);
    float envelope = smoothstep(0.0, 2.0 * aa, distance)
        * (1.0 - smoothstep(outer_limit - 2.0 * aa, outer_limit, distance));
    float alpha = clamp(coverage * envelope, 0.0, 1.0);
    rgb = clamp(rgb * BRIGHTNESS, 0.0, 1.0);
    return vec4(rgb, alpha);
}
