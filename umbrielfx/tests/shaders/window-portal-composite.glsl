vec4 transition_composite(vec2 uv) {
    vec4 color = umbriel_sample_composed(uv);
    float radius = length((uv - vec2(0.5)) * vec2(1.0, umbriel_output_size.y / umbriel_output_size.x));
    float ring = exp(-abs(radius - (0.1 + 0.5 * umbriel_clamped_progress)) * 35.0);
    float glow = ring * portal_envelope() * (0.2 + 0.2 * umbriel_audio_level());
    return vec4(mix(color.rgb, vec3(0.5, 0.2, 0.9) * color.a, glow), color.a);
}
