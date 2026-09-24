#include "config/shader_builtin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace umbriel {
  std::optional<AnimationShaderSource> builtinShader(std::string_view name, double amount, int kelvin) {
    const std::string start = "vec4 postprocess(vec3 p) { vec4 c = tex2D_screen(p.xy); ";
    std::string body;
    if (name == "grayscale")
      body = "float l=dot(c.rgb,vec3(0.2126,0.7152,0.0722)); return vec4(vec3(l),c.a); }";
    else if (name == "invert")
      body = "return vec4(c.a-c.rgb,c.a); }";
    else if (name == "saturation")
      body = std::format(
          "float l=dot(c.rgb,vec3(0.2126,0.7152,0.0722)); return vec4(clamp(mix(vec3(l),c.rgb,{:.6f}),0.0,c.a),c.a); "
          "}}",
          std::clamp(amount, 0.0, 10.0)
      );
    else if (name == "temperature") {
      // Black-body multipliers, normalised so 6500 K is identity.
      const auto raw = [](double k) {
        const double t = std::clamp(k, 1000.0, 40000.0) / 100.0;
        return std::array{
            std::clamp(t <= 66 ? 255.0 : 329.698727446 * std::pow(t - 60, -0.1332047592), 0.0, 255.0) / 255,
            std::clamp(
                t <= 66 ? 99.470802586 * std::log(t) - 161.119568166 : 288.122169528 * std::pow(t - 60, -0.0755148492),
                0.0, 255.0
            ) / 255,
            std::clamp(
                t >= 66       ? 255.0
                    : t <= 19 ? 0.0
                              : 138.517731223 * std::log(t - 10) - 305.044792729,
                0.0, 255.0
            ) / 255
        };
      };
      auto value = raw(kelvin);
      const auto white = raw(6500);
      for (size_t i = 0; i < 3; ++i)
        value[i] /= white[i];
      const double maximum = std::max({1.0, value[0], value[1], value[2]});
      body = std::format(
          "return vec4(c.rgb*vec3({:.6f},{:.6f},{:.6f}),c.a); }}", value[0] / maximum, value[1] / maximum,
          value[2] / maximum
      );
    } else
      return std::nullopt;
    return AnimationShaderSource{start + body, "builtin:" + std::string(name)};
  }

} // namespace umbriel
