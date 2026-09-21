precision highp float;
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float gain;
uniform bool linear;
void main() {
    vec3 light = 1.0 - exp(-max(texture2D(tex, v_texcoord).rgb, 0.0) * gain);
    if (linear) {
        light = mix(light / 12.92, pow((light + 0.055) / 1.055, vec3(2.4)),
            step(vec3(0.04045), light));
    }
    gl_FragColor = vec4(light, max(light.r, max(light.g, light.b)));
}
