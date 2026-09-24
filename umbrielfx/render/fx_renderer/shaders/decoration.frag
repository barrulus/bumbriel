precision highp float;
varying vec2 v_texcoord;
uniform vec2 ring_size;
uniform vec2 ring_raster;
uniform vec2 ring_origin;
uniform vec4 ring_radius;
uniform float ring_width;
uniform float ring_padding;
uniform float umbriel_time;
uniform float umbriel_scale;
uniform vec4 ring_color_base;
uniform bool ring_linear;
uniform bool ring_emission;
uniform float ring_threshold;
uniform vec4 ring_emission_bounds;



float ring_distance(vec2 coords) {
    vec2 half_size = ring_size * 0.5;
    vec2 p = coords - half_size;
    float radius = p.y < 0.0
        ? (p.x < 0.0 ? ring_radius.x : ring_radius.y)
        : (p.x < 0.0 ? ring_radius.w : ring_radius.z);
    radius = min(radius, min(half_size.x, half_size.y));
    vec2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}
vec4 ring_base_color(vec2 coords) { return ring_color_base; }
vec4 umbriel_palette_fallback() { return ring_color_base; }

vec4 ring_color(vec2 coords);
vec3 ring_srgb_to_linear(vec3 rgb) {
    return mix(rgb / 12.92, pow(max((rgb + 0.055) / 1.055, 0.0), vec3(2.4)),
        step(vec3(0.04045), rgb));
}
void main() {
    vec2 coords = v_texcoord * ring_raster - ring_origin;
    if (ring_emission && (any(lessThan(coords, ring_emission_bounds.xy))
        || any(greaterThan(coords, ring_emission_bounds.zw)))) {
        gl_FragColor = vec4(0.0);
        return;
    }
    vec4 value = ring_color(coords);
    float half_px = 0.5 / max(umbriel_scale, 0.01);
    float hole = smoothstep(-half_px, half_px, ring_distance(coords));
    value.a = clamp(value.a, 0.0, 1.0) * ring_color_base.a * hole;
    if (ring_emission) {
        vec3 light = value.rgb * value.a;
        float peak = max(light.r, max(light.g, light.b));
        gl_FragColor = vec4(light, value.a) * smoothstep(ring_threshold,
            max(ring_threshold + 0.001, 1.0), peak);
        return;
    }
    if (ring_linear) value.rgb = ring_srgb_to_linear(value.rgb);
    gl_FragColor = vec4(value.rgb * value.a, value.a);
}
