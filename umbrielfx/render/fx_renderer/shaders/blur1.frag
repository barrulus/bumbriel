#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying highp vec2 v_texcoord;
uniform sampler2D tex;

uniform float radius;
uniform vec2 halfpixel;
uniform vec4 sample_bounds;

vec4 sample_texel(vec2 uv) { return texture2D(tex, clamp(uv, sample_bounds.xy, sample_bounds.zw)); }

void main() {
  vec2 uv = v_texcoord * 2.0;

  vec4 sum = sample_texel(uv) * 4.0;
  sum += sample_texel(uv - halfpixel.xy * radius);
  sum += sample_texel(uv + halfpixel.xy * radius);
  sum += sample_texel(uv + vec2(halfpixel.x, -halfpixel.y) * radius);
  sum += sample_texel(uv - vec2(halfpixel.x, -halfpixel.y) * radius);

  gl_FragColor = sum / 8.0;
}
