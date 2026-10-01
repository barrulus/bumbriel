vec4 transition_fragment(vec2 uv, vec2 output_uv) {
    vec4 color = umbriel_sample_item(uv);
    vec2 source_position = umbriel_capture_extent.xy + uv * umbriel_capture_extent.zw;
    vec2 owner_uv = (source_position - umbriel_current_box.xy) / max(umbriel_current_box.zw, vec2(1.0));
    if (umbriel_item_token == umbriel_target_token && umbriel_target_token != 0) {
        float presence = portal_presence();
        if (presence <= 0.0) return vec4(0.0);
        if (presence >= 1.0) return color;
        float radius = presence * 1.5;
        color *= 1.0 - smoothstep(radius - 0.04, radius + 0.04, length(owner_uv * 2.0 - 1.0));
    }
    return color;
}
