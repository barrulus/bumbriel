vec4 transition_composite(vec2 uv) {
    vec4 color = umbriel_sample_composed(uv);
    float ripple = (0.5 + 0.5 * water_wave(uv * umbriel_output_size)) *
        water_envelope() * (0.04 + 0.04 * umbriel_audio_level());
    return vec4(mix(color.rgb, vec3(0.1, 0.45, 0.8) * color.a, ripple), color.a);
}
