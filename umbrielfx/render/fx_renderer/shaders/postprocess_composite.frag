precision highp float;
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform bool linear;
uniform bool mask;
uniform vec2 size;
uniform vec4 radius;
vec3 to_linear(vec3 c) {
    return mix(c / 12.92, pow(max((c + 0.055) / 1.055, 0.0), vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
    vec2 p = (v_texcoord - 0.5) * size;
    float r = p.y < 0.0 ? (p.x < 0.0 ? radius.x : radius.y) : (p.x < 0.0 ? radius.w : radius.z);
    r = min(r, min(size.x, size.y) * 0.5);
    vec2 q = abs(p) - size * 0.5 + r;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    // Preserve the native antialiased corner fringe. Straight edges are already
    // clipped by the quad: their last pixel is mathematically at d == -0.5,
    // but interpolation roundoff can put it just above the threshold and drop
    // a whole edge row/column. Only apply this distance test inside corner arcs.
    bool corner = q.x > 0.0 && q.y > 0.0;
    if (mask && corner && d > -0.5) discard;
    vec4 value = texture2D(tex, v_texcoord);
    if (linear && value.a > 0.0) value.rgb = to_linear(value.rgb / value.a) * value.a;
    gl_FragColor = value;
}
