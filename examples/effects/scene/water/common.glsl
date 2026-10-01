// Direction is +1 opening, -1 closing.
// Target inputs bypass the lifecycle coverage replaced by this shader;
// unrelated opacity is already present in the premultiplied input.
float water_envelope() {
    float p = umbriel_clamped_progress;
    return 4.0 * p * (1.0 - p);
}
float water_presence() {
    return umbriel_direction < 0.0 ? 1.0 - umbriel_clamped_progress : umbriel_clamped_progress;
}
float water_wave(vec2 position) {
    return sin(position.x * 0.075 + position.y * 0.04 +
        umbriel_random_seed.x * 6.2831853 + umbriel_clamped_progress * 4.0);
}
