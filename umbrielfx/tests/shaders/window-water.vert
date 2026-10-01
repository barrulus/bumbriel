vec4 transition_vertex(vec2 uv) {
    vec2 position = umbriel_current_box.xy + uv * umbriel_current_box.zw;
    if (umbriel_item_token != 0) {
        float amplitude = water_envelope() * (4.0 + 4.0 * umbriel_audio_level());
        position.y += water_wave(position) * amplitude;
        position.x += sin(float(umbriel_item_token) + umbriel_clamped_progress * 3.0) * amplitude;
    }
    return vec4(position / umbriel_output_size * 2.0 - 1.0, 0.0, 1.0);
}
