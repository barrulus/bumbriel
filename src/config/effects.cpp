#include "config/effects.h"

#include "config/section.h"
#include "config/shader_builtin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace umbriel {
  namespace {
    constexpr auto kError = ConfigDiagnostic::Severity::Error;

    void error(std::vector<ConfigDiagnostic>& diagnostics, const toml::node& node, std::string message) {
      diagnostics.push_back(makeDiagnostic(kError, node.source(), std::move(message)));
    }

    void error(std::vector<ConfigDiagnostic>& diagnostics, const EffectOrigin& origin, std::string message) {
      diagnostics.push_back({kError, std::move(message), origin.file, origin.line, origin.column});
    }

    std::optional<EffectNumber> number(const toml::node& node) {
      if (node.is_integer()) {
        const auto value = *node.value<int64_t>();
        if (value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max())
          return static_cast<int32_t>(value);
      } else if (node.is_floating_point()) {
        const auto value = *node.value<double>();
        if (std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max())
          return static_cast<float>(value);
      }
      return std::nullopt;
    }

    std::optional<EffectParam> parameter(const toml::node& node) {
      if (node.is_boolean())
        return *node.value<bool>();
      if (const auto value = number(node))
        return std::visit([](auto value) -> EffectParam { return value; }, *value);
      if (const auto* array = node.as_array(); array && array->size() >= 2 && array->size() <= 4) {
        std::vector<EffectNumber> values;
        for (const auto& entry : *array) {
          const auto value = number(entry);
          if (!value)
            return std::nullopt;
          values.push_back(*value);
        }
        return values;
      }
      return std::nullopt;
    }

    bool uniformName(std::string_view name) {
      if (name.empty()
          || name.starts_with("umbriel_")
          || name.starts_with("ring_")
          || name.starts_with("effect_")
          || name.starts_with("gl_"))
        return false;
      constexpr std::array<std::string_view, 2> host{"proj", "tex_proj"};
      if (std::ranges::contains(host, name))
        return false;
      if (std::isdigit(static_cast<unsigned char>(name.front())))
        return false;
      return std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }

    EffectPass readPass(
        const toml::table& table, EffectScope scope, size_t index, std::vector<ConfigDiagnostic>& diagnostics,
        std::vector<std::filesystem::path>& watches
    ) {
      EffectPass pass;
      pass.origin = effectOrigin(table);
      Section section(table, "effect pass", diagnostics, kError);
      const bool postprocess = persistentEffect(scope) && (scope != EffectScope::BorderOuter || index > 0);
      section.text("builtin", pass.builtin);
      const bool builtin = section.node("builtin") != nullptr;
      const bool shader = section.node("shader") != nullptr;
      if (builtin == shader)
        error(diagnostics, table, "effect pass requires exactly one of shader or builtin");
      if (section.node("buffer") && (!postprocess || builtin))
        error(diagnostics, *section.node("buffer"), "buffer is only supported on file-backed postprocess passes");
      section.boolean("buffer", pass.buffer);
      auto read = readAnimationShader(section, diagnostics, kError);
      watches.insert(watches.end(), read.watchPaths.begin(), read.watchPaths.end());
      if (read.source)
        pass.source = std::move(*read.source);
      const auto* paramsNode = section.take("params");
      const auto* params = paramsNode ? paramsNode->as_table() : nullptr;
      if (paramsNode && !params)
        error(diagnostics, *paramsNode, "params must be a flat table of scalar or numeric vector uniforms");
      if (builtin) {
        double amount = 1.5;
        int kelvin = 4000;
        if (!postprocess)
          error(diagnostics, table, "builtins require a postprocess pass");
        if (params) {
          Section values(*params, "builtin params", diagnostics, kError);
          if (pass.builtin == "saturation")
            values.real("amount", 0, 10, amount);
          else if (pass.builtin == "temperature")
            values.integer("kelvin", 1000, 40000, kelvin);
        }
        if (const auto source = builtinShader(pass.builtin, amount, kelvin))
          pass.source = *source;
        else
          error(diagnostics, table, "unknown builtin: " + pass.builtin);
      }
      if (params)
        for (const auto& [name, node] : *params) {
          const auto value = parameter(node);
          if (!builtin && !uniformName(name.str()))
            error(diagnostics, node, "reserved or invalid uniform name: " + std::string(name.str()));
          if (!value)
            error(
                diagnostics, node, "parameter requires a boolean, int32, finite float, or numeric vector of length 2–4"
            );
          else
            pass.params.emplace(name.str(), *value);
        }
      return pass;
    }

    EffectPipeline readPipeline(
        const toml::table& table, EffectScope scope, std::vector<ConfigDiagnostic>& diagnostics,
        std::vector<std::filesystem::path>& watches
    ) {
      EffectPipeline pipeline;
      pipeline.origin = effectOrigin(table);
      Section section(table, std::string(kEffectScopeNames[static_cast<size_t>(scope)]), diagnostics, kError);
      section.boolean("enabled", pipeline.enabled);
      if (!pipeline.enabled)
        return pipeline;
      if (scope == EffectScope::Drag) {
        pipeline.drag.emplace();
        auto& parameters = pipeline.drag->parameters;
        const auto read = [&](std::string_view key, float& target, double minimum, double maximum) {
          double value = target;
          section.real(key, minimum, maximum, value);
          target = static_cast<float>(value);
        };
        read("stiffness", parameters.stiffness, 1, 1000);
        read("coupling", parameters.coupling, 0, 500);
        read("damping", parameters.damping, 0.5, 60);
        read("pointer_response", parameters.pointer_response, 0, 10);
        read("stiffness_gradient", parameters.stiffness_gradient, -0.9, 1);
        read("lag_gradient", parameters.lag_gradient, -0.9, 4);
        read("downward_pull", parameters.downward_pull, 0, 50);
        read("motion_gain", parameters.motion_gain, 0, 32);
        read("decay", parameters.decay, 0.1, 30);
        if (table.empty())
          error(diagnostics, table, "drag preset requires simulation parameters or enabled=true");
        return pipeline;
      }
      section.boolean("palette", pipeline.palette);
      if (persistentEffect(scope))
        section.boolean("animated", pipeline.animated).real("speed", 0, 10, pipeline.speed);
      if (scope == EffectScope::BorderInner || scope == EffectScope::BorderOuter)
        section.boolean("focused_only", pipeline.focusedOnly);
      if (scope == EffectScope::BorderOuter) {
        section.integer("padding", 0, 1024, pipeline.padding);
        section.sub("light", [&](Section& light) {
          light.boolean("enabled", pipeline.light.enabled)
              .real("spread", 1, 256, pipeline.light.spread)
              .real("intensity", 0, 4, pipeline.light.intensity)
              .real("threshold", 0, 1, pipeline.light.threshold);
        });
      }
      if (scope == EffectScope::Overlay)
        section.integer("cursor_radius", 0, 4096, pipeline.cursorRadius);
      if (scope == EffectScope::Open || scope == EffectScope::Close) {
        section.integer("duration_ms", 1, 10000, pipeline.durationMs);
        if (section.node("curve")) {
          std::string curve;
          section.text("curve", curve);
          pipeline.curve = curve;
        }
      }
      const auto* node = section.take("passes");
      const auto* passes = node ? node->as_array() : nullptr;
      if (!passes || passes->empty() || passes->size() > 16) {
        error(diagnostics, node ? *node : table, "enabled effect leaf requires 1–16 passes");
        return pipeline;
      }
      for (const auto& pass : *passes) {
        if (!pass.is_table()) {
          error(diagnostics, pass, "effect pass must be a table");
          continue;
        }
        pipeline.passes.push_back(readPass(*pass.as_table(), scope, pipeline.passes.size(), diagnostics, watches));
      }
      return pipeline;
    }

    EffectMask definitionMask(const EffectLibrary& library, const EffectDefinition& definition) {
      if (!definition.choose)
        return definition.mask();
      if (definition.choose->empty())
        return 0;
      const auto candidate = library.find(definition.choose->front());
      return candidate == library.end() ? 0 : candidate->second.mask();
    }
  } // namespace

  EffectOrigin effectOrigin(const toml::node& node) {
    const auto& source = node.source();
    return {source.path ? *source.path : "", source.begin.line, source.begin.column};
  }

  std::optional<EffectAction> parseEffectAction(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::string kind;
    EffectAction action;
    if (!(input >> kind >> action.operation))
      return std::nullopt;
    if (kind == "global")
      action.owner = EffectOwner::Global;
    else if (kind == "output")
      action.owner = EffectOwner::Output;
    else if (kind == "window")
      action.owner = EffectOwner::Window;
    else if (kind == "layer")
      action.owner = EffectOwner::Layer;
    else if (kind == "region")
      action.owner = EffectOwner::Region;
    else if (kind == "system")
      action.system = true;
    else
      return std::nullopt;
    const std::array<std::string_view, 6> operations{"set", "cycle", "off", "on", "toggle", "default"};
    if (!std::ranges::contains(operations, action.operation))
      return std::nullopt;
    action.mask = effectMask(action.owner);
    bool target = false, scope = false;
    std::string token;
    while (input >> std::quoted(token)) {
      if (token == "--target" || token == "--scope") {
        const bool isTarget = token == "--target";
        if (action.system || (isTarget ? target : scope) || !(input >> std::quoted(token)) || token.empty())
          return std::nullopt;
        if (isTarget) {
          target = true;
          action.target = token;
        } else {
          scope = true;
          action.mask = 0;
          std::string_view remaining = token;
          while (!remaining.empty()) {
            const auto comma = remaining.find(',');
            const auto leaf = effectScope(remaining.substr(0, comma));
            if (!leaf || (action.mask & effectBit(*leaf)) || !(effectMask(action.owner) & effectBit(*leaf)))
              return std::nullopt;
            action.mask |= effectBit(*leaf);
            if (comma == std::string_view::npos)
              break;
            remaining.remove_prefix(comma + 1);
            if (remaining.empty())
              return std::nullopt;
          }
        }
      } else {
        if (token.starts_with("--") || !validEffectName(token) || std::ranges::contains(action.names, token))
          return std::nullopt;
        action.names.push_back(token);
      }
    }
    if (!input.eof()
        || (target && action.owner == EffectOwner::Global)
        || ((action.owner == EffectOwner::Layer || action.owner == EffectOwner::Region) && !target))
      return std::nullopt;
    if (action.operation == "set") {
      if (action.system || action.names.empty())
        return std::nullopt;
    } else if (action.operation == "cycle") {
      if (action.system || action.names.size() != 1)
        return std::nullopt;
    } else if (!action.names.empty())
      return std::nullopt;
    return action;
  }

  std::string formatEffectAction(const EffectAction& action) {
    constexpr std::array<std::string_view, 5> kinds{"global", "output", "window", "layer", "region"};
    std::ostringstream text;
    text << (action.system ? "system" : kinds[static_cast<size_t>(action.owner)]) << ' ' << action.operation;
    for (const auto& name : action.names)
      text << ' ' << std::quoted(name);
    if (!action.target.empty())
      text << " --target " << std::quoted(action.target);
    if (action.mask != effectMask(action.owner)) {
      text << " --scope ";
      bool first = true;
      for (size_t i = 0; i < kEffectScopeCount; ++i)
        if (action.mask & (EffectMask{1} << i)) {
          if (!first)
            text << ',';
          text << kEffectScopeNames[i];
          first = false;
        }
    }
    return text.str();
  }

  EffectMask effectMask(EffectOwner owner) {
    switch (owner) {
    case EffectOwner::Window:
      return kWindowEffects;
    case EffectOwner::Layer:
      return kLayerEffects;
    case EffectOwner::Region:
      return effectBit(EffectScope::Screen);
    case EffectOwner::Global:
    case EffectOwner::Output:
      return kAllEffects;
    }
    return 0;
  }

  std::optional<EffectScope> effectScope(std::string_view name) {
    const auto found = std::ranges::find(kEffectScopeNames, name);
    if (found == kEffectScopeNames.end())
      return std::nullopt;
    return static_cast<EffectScope>(found - kEffectScopeNames.begin());
  }

  bool persistentEffect(EffectScope scope) {
    return scope == EffectScope::Content
        || scope == EffectScope::BorderInner
        || scope == EffectScope::BorderOuter
        || scope == EffectScope::Screen
        || scope == EffectScope::Overlay;
  }

  bool validEffectName(std::string_view name) {
    return !name.empty()
        && std::ranges::none_of(name, [](unsigned char c) { return std::isspace(c) || c == ',' || c == '\0'; });
  }

  EffectMask EffectDefinition::mask() const {
    EffectMask result = 0;
    for (size_t i = 0; i < scopes.size(); ++i)
      if (scopes[i])
        result |= EffectMask{1} << i;
    return result;
  }

  EffectLibrary readEffectLibrary(
      const toml::node* node, std::vector<ConfigDiagnostic>& diagnostics, std::vector<std::filesystem::path>& watches
  ) {
    EffectLibrary library;
    if (!node)
      return library;
    const auto* table = node->as_table();
    if (!table) {
      error(diagnostics, *node, "effects must be a table of named definitions");
      return library;
    }
    for (const auto& [name, entry] : *table) {
      if (!validEffectName(name.str()))
        error(diagnostics, entry, "effect names must be nonempty and contain no whitespace or commas");
      if (!entry.is_table()) {
        error(diagnostics, entry, "effect definition must be a table");
        continue;
      }
      EffectDefinition definition;
      definition.origin = effectOrigin(entry);
      Section section(*entry.as_table(), "effects." + std::string(name.str()), diagnostics, kError);
      if (section.node("choose")) {
        definition.choose.emplace();
        section.strings("choose", *definition.choose).text("selection", definition.selection);
        if (definition.choose->empty())
          error(diagnostics, entry, "choose requires a nonempty list of unique concrete effect names");
        if (definition.selection != "unused_first"
            && definition.selection != "round_robin"
            && definition.selection != "random")
          error(diagnostics, entry, "selection must be unused_first, round_robin or random");
      } else {
        const auto leaf = [&](Section& parent, std::string_view key, EffectScope scope) {
          parent.sub(key, [&](Section& child) {
            child.freeform();
            definition.scopes[static_cast<size_t>(scope)] = readPipeline(child.table(), scope, diagnostics, watches);
          });
        };
        for (size_t i = 0; i < kEffectScopeCount; ++i)
          if (!kEffectScopeNames[i].starts_with("border."))
            leaf(section, kEffectScopeNames[i], static_cast<EffectScope>(i));
        section.sub("border", [&](Section& border) {
          leaf(border, "inner", EffectScope::BorderInner);
          leaf(border, "outer", EffectScope::BorderOuter);
          leaf(border, "focus", EffectScope::BorderFocus);
          if (!border.allKeysKnown())
            error(diagnostics, border.table(), "border is a group; put padding under border.outer");
        });
        if (!definition.mask())
          error(diagnostics, entry, "concrete effect requires at least one leaf scope");
      }
      library.emplace(name.str(), std::move(definition));
    }
    for (const auto& [name, definition] : library) {
      if (!definition.choose)
        continue;
      std::set<std::string> seen;
      std::optional<EffectMask> expected;
      for (const auto& candidate : *definition.choose) {
        const auto found = library.find(candidate);
        if (!seen.insert(candidate).second || found == library.end() || found->second.choose) {
          error(
              diagnostics, definition.origin, "choice '" + name + "' requires unique concrete references: " + candidate
          );
          continue;
        }
        const auto mask = found->second.mask();
        if (expected && *expected != mask)
          error(
              diagnostics, definition.origin, "choice candidates must define the same leaves, including disabled leaves"
          );
        expected = mask;
        if ((mask & kWindowEffects) && (mask & kOutputEffects))
          error(diagnostics, definition.origin, "choice candidates cannot mix surface and output scopes");
      }
    }
    return library;
  }

  std::optional<EffectSelector> readEffectSelector(Section& section, std::vector<ConfigDiagnostic>& diagnostics) {
    const auto* node = section.take("effects");
    if (!node)
      return std::nullopt;
    EffectSelector selector;
    selector.origin = effectOrigin(*node);
    const auto* array = node->as_array();
    if (!array) {
      error(diagnostics, *node, "effects selector must be an array of effect names");
      return selector;
    }
    for (const auto& entry : *array) {
      const auto name = entry.value<std::string>();
      if (!name || !validEffectName(*name))
        error(diagnostics, entry, "effects selector entries must be valid effect names");
      else
        selector.names.push_back(*name);
    }
    return selector;
  }

  void readEffectPolicy(Section& section, EffectPolicy& policy, std::vector<ConfigDiagnostic>& diagnostics) {
    section.boolean("enabled", policy.enabled)
        .boolean("in_capture", policy.inCapture)
        .boolean("reads_cursor", policy.readsCursor)
        .text("redraw", policy.redraw)
        .integer("fps", 0, 240, policy.fps);
    if (policy.redraw != "auto" && policy.redraw != "on_damage" && policy.redraw != "continuous")
      error(diagnostics, section.table(), "render.effects.redraw must be auto, on_damage or continuous");
  }

  std::vector<EffectRegion> readEffectRegions(const toml::node* node, std::vector<ConfigDiagnostic>& diagnostics) {
    std::vector<EffectRegion> regions;
    if (!node)
      return regions;
    const auto* array = node->as_array();
    if (!array) {
      error(diagnostics, *node, "effect_region must be an array of tables");
      return regions;
    }
    std::set<std::string> names;
    for (const auto& entry : *array) {
      if (!entry.is_table()) {
        error(diagnostics, entry, "effect_region must be a table");
        continue;
      }
      EffectRegion region;
      region.origin = effectOrigin(entry);
      Section section(*entry.as_table(), "effect_region", diagnostics, kError);
      section.text("name", region.name)
          .text("output", region.output)
          .integer("x", -100000, 100000, region.x)
          .integer("y", -100000, 100000, region.y)
          .integer("width", 1, 100000, region.width)
          .integer("height", 1, 100000, region.height);
      const auto selector = readEffectSelector(section, diagnostics);
      if (!selector || region.name.empty() || !region.width || !region.height)
        error(diagnostics, entry, "effect_region requires name, positive width/height, and effects");
      if (!names.insert(region.name).second)
        error(diagnostics, entry, "duplicate effect_region name: " + region.name);
      if (selector)
        region.effects = *selector;
      regions.push_back(std::move(region));
    }
    return regions;
  }

  void validateEffectSelector(
      const EffectLibrary& library, const EffectSelector& selector, EffectOwner owner,
      std::vector<ConfigDiagnostic>& diagnostics, EffectMask mask
  ) {
    mask &= effectMask(owner);
    for (const auto& name : selector.names) {
      const auto found = library.find(name);
      if (found == library.end()) {
        error(diagnostics, selector.origin, "unknown effect: " + name);
        continue;
      }
      const auto leaves = definitionMask(library, found->second);
      if (!(leaves & mask))
        error(diagnostics, selector.origin, "effect '" + name + "' has no applicable leaf in the requested scopes");
      if (found->second.choose
          && (owner == EffectOwner::Layer || owner == EffectOwner::Region)
          && (leaves & ~effectMask(owner)))
        error(
            diagnostics, selector.origin, "choice '" + name + "' contains scopes outside this owner's lifetime family"
        );
    }
  }

  void applyEffects(
      ResolvedEffects& resolved, const EffectLibrary& library, const EffectSelector& selector, EffectMask mask,
      const EffectVariants& variants
  ) {
    const auto replace = [&](size_t i, std::optional<EffectPipeline> pipeline, EffectAssignment source) {
      auto& leaf = resolved[i];
      if (!leaf.source.effect.empty() || !leaf.source.assignment.file.empty())
        leaf.overridden.push_back(leaf.source);
      leaf.pipeline = std::move(pipeline);
      leaf.runtimeDisabled = false;
      leaf.source = std::move(source);
    };
    if (selector.names.empty()) {
      for (size_t i = 0; i < resolved.size(); ++i)
        if (mask & (EffectMask{1} << i))
          replace(i, std::nullopt, {"", "", selector.origin, {}});
      return;
    }
    for (const auto& name : selector.names) {
      auto found = library.find(name);
      if (found == library.end())
        continue;
      std::string choice;
      if (found->second.choose) {
        const auto variant = variants.find(name);
        if (variant == variants.end() || !std::ranges::contains(*found->second.choose, variant->second))
          continue;
        choice = name;
        found = library.find(variant->second);
        if (found == library.end() || found->second.choose)
          continue;
      }
      for (size_t i = 0; i < resolved.size(); ++i)
        if ((mask & (EffectMask{1} << i)) && found->second.scopes[i])
          replace(i, found->second.scopes[i], {found->first, choice, selector.origin, found->second.scopes[i]->origin});
    }
  }

  ResolvedEffects resolveEffects(
      const EffectLibrary& library, std::span<const EffectSelector> selectors, EffectMask mask,
      const EffectVariants& variants
  ) {
    ResolvedEffects resolved;
    for (const auto& selector : selectors)
      applyEffects(resolved, library, selector, mask, variants);
    return resolved;
  }

  bool sameEffectPipeline(const EffectPipeline& a, const EffectPipeline& b) {
    auto left = a;
    auto right = b;
    const auto clearOrigins = [](EffectPipeline& pipeline) {
      pipeline.origin = {};
      for (auto& pass : pipeline.passes) {
        pass.origin = {};
        pass.source.file.clear();
      }
    };
    clearOrigins(left);
    clearOrigins(right);
    return left == right;
  }
} // namespace umbriel
