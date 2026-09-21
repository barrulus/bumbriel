// Adapted from Biri resources/shaders/focus-ring/pulse.frag.
// Barrulus shader collection; GPL-3.0, see ../LICENSE.
// A second, independent ring shader: cyan pigment gently brightens and fades.
// No deformation, so it needs no extra padding. Coordinates use logical pixels.
vec4 ring_color(vec2 coords) {
    float d = ring_distance(coords);
    float half_px = 0.5 / umbriel_scale;
    float coverage = smoothstep(-half_px, half_px, d)
        * (1.0 - smoothstep(ring_width - half_px, ring_width + half_px, d));
    float pulse = 0.65 + 0.35 * sin(umbriel_time * 2.0);
    return vec4(vec3(0.15, 0.8, 1.0) * pulse, coverage);
}
