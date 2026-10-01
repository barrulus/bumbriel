vec4 transition_fragment(vec2 uv, vec2 output_uv) {
    vec4 color = umbriel_sample_item(uv);
    vec2 source_position = umbriel_capture_extent.xy + uv * umbriel_capture_extent.zw;
    vec2 owner_uv = (source_position - umbriel_current_box.xy) / max(umbriel_current_box.zw, vec2(1.0));
    if (umbriel_item_token == umbriel_target_token && umbriel_target_token != 0) {
        float presence = water_presence();
        if (presence <= 0.0) return vec4(0.0);
        if (presence >= 1.0) return color;
        float front = presence + sin(owner_uv.x * 12.0 + umbriel_random_seed.y * 6.2831853) *
            0.12 * water_envelope();
        color *= smoothstep(owner_uv.y - 0.05, owner_uv.y + 0.05, front);
    }
    return color;
}
