precision highp float;
varying vec2 v_texcoord;
uniform vec2 umbriel_size;
uniform vec2 umbriel_output_size;
uniform vec2 umbriel_cursor;
uniform vec4 umbriel_region;
uniform float umbriel_time;
uniform float umbriel_scale;
uniform sampler2D effect_screen, effect_source, effect_previous, effect_screen_previous, effect_buffer;
uniform mat3 effect_source_matrix;
uniform bool effect_first, effect_linear;

vec3 effect_to_srgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(max(c, 0.0), vec3(1.0/2.4)) - 0.055,
        step(vec3(0.0031308), c));
}
vec4 effect_decode(vec4 c) {
    if (!effect_linear || c.a <= 0.0) return c;
    return vec4(effect_to_srgb(c.rgb / c.a) * c.a, c.a);
}
vec2 effect_uv(vec2 uv) { return (uv - umbriel_region.xy) / umbriel_region.zw; }
bool effect_outside(vec2 uv) { return any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1))); }
vec2 effect_source_uv(vec2 uv) { return (vec3(uv, 1) * effect_source_matrix).xy; }
vec4 tex2D_screen(vec2 uv) {
    uv = effect_uv(uv);
    if (effect_outside(uv)) return vec4(0);
    return effect_first ? effect_decode(texture2D(effect_screen, effect_source_uv(uv))) : texture2D(effect_screen, uv);
}
vec4 tex2D_source(vec2 uv) {
    uv = effect_uv(uv);
    return effect_outside(uv) ? vec4(0) : effect_decode(texture2D(effect_source, effect_source_uv(uv)));
}
vec4 tex2D_prev(vec2 uv) {
    uv = effect_uv(uv);
    return effect_outside(uv) ? vec4(0) : texture2D(effect_previous, uv);
}
vec4 tex2D_screen_prev(vec2 uv) {
    uv = effect_uv(uv);
    return effect_outside(uv) ? vec4(0) : effect_decode(texture2D(effect_screen_previous, effect_source_uv(uv)));
}
vec4 tex2D_buffer(vec2 uv) {
    uv = effect_uv(uv);
    return effect_outside(uv) ? vec4(0) : texture2D(effect_buffer, uv);
}
