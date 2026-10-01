// Editable carousel geometry. Every face keeps one equal output-aspect canvas.
// progress: 0 is the native-quality landing; 1 is the held presentation.
uniform float max_elevation_degrees;
vec4 transition_vertex(vec2 uv) {
    float count = float(umbriel_scene_count);
    vec2 plane = uv * 2.0 - 1.0;
    float angle = 0.0;
    float radius = 0.0;
    if (umbriel_scene_count > 1) {
        angle = 6.2831853 * (float(umbriel_item_ordinal) - umbriel_navigation_position) / count;
        // A two-sided prism has zero radius. Two distinct orbiting planes use
        // a finite radius and remain readable halfway between selections.
        radius = umbriel_scene_count == 2 ? 1.0 : 1.0 / tan(3.14159265 / count);
    }
    float orientation = umbriel_scene_count == 2 ? sin(angle) * 1.04719755 : angle;
    vec3 position = vec3(cos(orientation) * plane.x + radius * sin(angle),
        plane.y, radius * cos(angle) - sin(orientation) * plane.x);
    // Orbit above/below the front face without moving its centre.
    // TOML controls the maximum angle; native landing returns to zero tilt.
    float elevation = (1.0 - 2.0 * clamp(umbriel_pointer.y, 0.0, 1.0))
        * radians(clamp(max_elevation_degrees, 0.0, 90.0))
        * umbriel_clamped_progress;
    vec2 relative = vec2(position.y, position.z - radius);
    position.y = cos(elevation) * relative.x + sin(elevation) * relative.y;
    position.z = radius - sin(elevation) * relative.x + cos(elevation) * relative.y;
    float w = radius + 2.0 - position.z;
    float scale = 2.0 * mix(1.0, 0.68 * umbriel_zoom, umbriel_clamped_progress);
    float pulse = umbriel_clamped_progress * 0.04 * umbriel_audio_level();
    scale += pulse;
    float depth = (w - 1.0) / (2.0 * radius + 3.0) * 2.0 - 1.0;
    return vec4(position.xy * scale, depth * w, w);
}
