uniform vec4 umbriel_palette[8];
uniform int umbriel_palette_count;
vec4 umbriel_palette_at(float position) {
    if (umbriel_palette_count <= 0) return umbriel_palette_fallback();
    float span = float(umbriel_palette_count);
    float scaled = fract(position) * span;
    float index = floor(scaled);
    float next = mod(index + 1.0, span);
    vec4 from = umbriel_palette[0];
    vec4 to = umbriel_palette[0];
    for (int i = 0; i < 8; i++) {
        if (i >= umbriel_palette_count) break;
        if (float(i) == index) from = umbriel_palette[i];
        if (float(i) == next) to = umbriel_palette[i];
    }
    return mix(from, to, scaled - index);
}
