#include "config/shaders.h"

#include "config/section.h"
#include "config/store.h"

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
      // Biri's reference-normalized black-body approximation. At 6500 K every
      // multiplier is exactly one; do not substitute the bundled warm tint.
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

  void readShaders(Section& root, Config& loaded) {
    auto& settings = loaded.shaders;
    auto& diagnostics = configStore().mutableDiagnostics();
    const auto warn = [&](const toml::node& node, std::string message) {
      diagnostics.push_back(makeDiagnostic(ConfigDiagnostic::Severity::Warning, node.source(), std::move(message)));
    };
    root.sub("shaders", [&](Section& section) {
      section.text("window", settings.window)
          .text("output", settings.output)
          .text("global", settings.global)
          .text("redraw", settings.redraw)
          .boolean("enabled", settings.enabled)
          .boolean("in_capture", settings.inCapture)
          .boolean("reads_cursor", settings.readsCursor);
      section.sub("preset", [&](Section& presets) {
        presets.freeform();
        for (const auto& [name, node] : presets.table()) {
          const auto* table = node.as_table();
          if (table == nullptr) {
            warn(node, "shader preset must be a table");
            continue;
          }
          Config::Shaders::Preset preset;
          preset.name = std::string(name.str());
          Section keys(*table, "shaders.preset." + preset.name, diagnostics);
          keys.text("scope", preset.scope).integer("cursor_radius", 0, 4096, preset.cursorRadius);
          if (preset.scope != "window" && preset.scope != "output" && preset.scope != "global") {
            warn(node, "shader preset scope must be window, output or global; using global");
            preset.scope = "global";
          }
          const auto* passes = keys.take("passes");
          if (passes != nullptr && passes->is_array()) {
            for (const auto& passNode : *passes->as_array()) {
              const auto* passTable = passNode.as_table();
              if (passTable == nullptr) {
                warn(passNode, "shader pass must be a table");
                preset.passes.push_back({});
                continue;
              }
              Config::Shaders::Pass pass;
              Section passKeys(*passTable, "shaders.preset." + preset.name + ".passes", diagnostics);
              passKeys.boolean("buffer", pass.buffer);
              std::string builtin;
              double amount = 1.5;
              int kelvin = 4000;
              passKeys.text("preset", builtin).real("amount", 0.0, 10.0, amount).integer("kelvin", 1000, 40000, kelvin);
              auto result = readAnimationShader(passKeys, diagnostics);
              for (auto& path : result.watchPaths)
                configStore().addWatchPath(std::move(path));
              pass.source = builtin.empty() ? std::move(result.source) : builtinShader(builtin, amount, kelvin);
              if (!pass.source)
                warn(passNode, "shader pass requires a readable shader or a known builtin preset");
              if (!builtin.empty() && passKeys.node("shader") != nullptr) {
                warn(passNode, "shader pass cannot specify both shader and preset");
                pass.source.reset();
              }
              preset.passes.push_back(std::move(pass));
            }
          }
          if (preset.passes.empty() || preset.passes.size() > 16) {
            warn(node, "shader preset requires 1 to 16 passes; disabling preset");
            preset.passes.clear();
          }
          settings.presets.push_back(std::move(preset));
        }
      });
      if (settings.redraw != "auto" && settings.redraw != "on-damage" && settings.redraw != "continuous") {
        warn(section.table(), "shaders.redraw must be auto, on-damage or continuous; using auto");
        settings.redraw = "auto";
      }
      const auto* regions = section.take("region");
      if (regions != nullptr && !regions->is_array())
        warn(*regions, "shaders.region must be an array of tables");
      if (regions != nullptr && regions->is_array())
        for (const auto& node : *regions->as_array()) {
          if (!node.is_table()) {
            warn(node, "shader region must be a table");
            continue;
          }
          Config::Shaders::Region region;
          Section keys(*node.as_table(), "shaders.region", diagnostics);
          keys.text("output", region.output)
              .text("preset", region.preset)
              .integer("x", -100000, 100000, region.x)
              .integer("y", -100000, 100000, region.y)
              .integer("width", 1, 100000, region.width)
              .integer("height", 1, 100000, region.height);
          if (region.width > 0 && region.height > 0 && !region.preset.empty())
            settings.regions.push_back(std::move(region));
          else
            warn(node, "shader region requires preset, width and height");
        }
    });
  }
} // namespace umbriel
