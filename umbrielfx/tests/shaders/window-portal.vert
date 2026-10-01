vec4 transition_vertex(vec2 uv) {
    vec2 position = umbriel_current_box.xy + uv * umbriel_current_box.zw;
    if (umbriel_item_token != 0) {
        float strength = portal_envelope() * (4.0 + 4.0 * umbriel_audio_level());
        vec2 center = umbriel_current_box.xy + umbriel_current_box.zw * 0.5;
        vec2 away = center / umbriel_output_size - vec2(0.5);
        position += away * strength * 3.0;
        if (umbriel_item_token == umbriel_target_token) {
            position += vec2(sin(uv.y * 3.14159265), sin(uv.x * 3.14159265)) * strength;
        }
    }
    return vec4(position / umbriel_output_size * 2.0 - 1.0, 0.0, 1.0);
}
