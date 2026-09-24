#include "config/config.h"
#include "config/store.h"
#include "layer/layer_surface.h"
#include "output/output.h"
#include "server/ipc_commands.h"
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"

#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace umbriel {
  namespace {
    using Json = nlohmann::json;

    Json origin(const EffectOrigin& value) {
      return {{"file", value.file}, {"line", value.line}, {"column", value.column}};
    }

    Json assignment(const EffectAssignment& value) {
      return {
          {"effect", value.effect},
          {"choice", value.choice},
          {"assignment", origin(value.assignment)},
          {"definition", origin(value.definition)}
      };
    }

    Json pipeline(const EffectPipeline& value) {
      Json result{{"enabled", value.enabled}, {"definition", origin(value.origin)}};
      if (!value.enabled)
        return result;
      if (value.drag) {
        const auto& p = value.drag->parameters;
        result["physics"] = {
            {"stiffness", p.stiffness},
            {"coupling", p.coupling},
            {"damping", p.damping},
            {"pointer_response", p.pointer_response},
            {"stiffness_gradient", p.stiffness_gradient},
            {"lag_gradient", p.lag_gradient},
            {"downward_pull", p.downward_pull},
            {"motion_gain", p.motion_gain},
            {"decay", p.decay}
        };
        return result;
      }
      result["palette"] = value.palette;
      result["animated"] = value.animated;
      result["speed"] = value.speed;
      result["focused_only"] = value.focusedOnly;
      result["padding"] = value.padding;
      result["cursor_radius"] = value.cursorRadius;
      result["light"] = {
          {"enabled", value.light.enabled},
          {"spread", value.light.spread},
          {"intensity", value.light.intensity},
          {"threshold", value.light.threshold}
      };
      if (value.durationMs)
        result["duration_ms"] = *value.durationMs;
      if (value.curve)
        result["curve"] = *value.curve;
      result["passes"] = Json::array();
      for (const auto& pass : value.passes) {
        Json item{{"definition", origin(pass.origin)}, {"buffer", pass.buffer}, {"params", Json::object()}};
        if (!pass.builtin.empty())
          item["builtin"] = pass.builtin;
        else
          item["shader"] = pass.source.file.string();
        for (const auto& [name, param] : pass.params)
          std::visit(
              [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::vector<EffectNumber>>) {
                  item["params"][name] = Json::array();
                  for (const auto& number : v)
                    std::visit([&](auto scalar) { item["params"][name].push_back(scalar); }, number);
                } else
                  item["params"][name] = v;
              },
              param
          );
        result["passes"].push_back(std::move(item));
      }
      return result;
    }

    Json resolved(const ResolvedEffect& value) {
      Json result{{"source", assignment(value.source)}, {"overridden", Json::array()}};
      result["pipeline"] = value.pipeline ? pipeline(*value.pipeline) : Json(nullptr);
      for (const auto& previous : value.overridden)
        result["overridden"].push_back(assignment(previous));
      return result;
    }

    template <typename Active> Json state(const EffectState& value, EffectMask mask, bool layer, Active active) {
      Json result{{"scopes", Json::object()}, {"leases", Json::object()}};
      for (const auto& [name, lease] : value.leases())
        result["leases"][name] = {{"variant", lease->preset}, {"reservation", lease->pool}};
      for (size_t i = 0; i < kEffectScopeCount; ++i) {
        const auto scope = static_cast<EffectScope>(i);
        if (!(mask & effectBit(scope)))
          continue;
        const auto& leaf = value.resolved()[i];
        auto entry = resolved(leaf);
        std::string suppression;
        if (value.disabled() & effectBit(scope) || leaf.runtimeDisabled)
          suppression = "runtime_off";
        else if (!leaf.pipeline)
          suppression = "unselected";
        else if (!leaf.pipeline->enabled)
          suppression = "disabled_leaf";
        else if (!effectsEnabled())
          suppression = "system_off";
        else if (!persistentEffect(scope) && !nativeEffectEnabled(scope, layer))
          suppression = "native_animation_off";
        entry["suppression"] = suppression.empty() ? Json(nullptr) : Json(suppression);
        const auto* event = active(scope);
        entry["active"] = event && event->custom() ? resolved(event->captured()) : Json(nullptr);
        entry["active_generation"] = event && event->custom() ? Json(event->generation()) : Json(nullptr);
        entry["retained"] = event && event->custom() && event->captured() != leaf;
        result["scopes"][kEffectScopeNames[i]] = std::move(entry);
      }
      return result;
    }
  } // namespace

  nlohmann::json IpcCommands::effects(Server& server, std::string_view arg) {
    std::string filter, target, extra;
    std::istringstream input{std::string(arg)};
    if (input >> filter) {
      if ((filter != "--window" && filter != "--output" && filter != "--layer" && filter != "--region")
          || !(input >> std::quoted(target))
          || target.empty()
          || input >> extra)
        return {{"err", "effects expects one --window, --output, --layer, or --region target"}};
    }
    const auto& cfg = config();
    Json result{
        {"library", Json::object()},
        {"outputs", Json::array()},
        {"windows", Json::array()},
        {"layers", Json::array()},
        {"regions", Json::array()}
    };
    result["generation"] = configStore().generation();
    result["policy"] = {
        {"enabled", effectsEnabled()},
        {"configured_enabled", cfg.effectPolicy.enabled},
        {"in_capture", cfg.effectPolicy.inCapture},
        {"reads_cursor", cfg.effectPolicy.readsCursor},
        {"redraw", cfg.effectPolicy.redraw},
        {"fps", cfg.effectPolicy.fps},
        {"native_animation", cfg.animation.enabled},
        {"native_drag_physics", cfg.animation.windowsMove.dragPhysics}
    };
    for (const auto& [name, definition] : cfg.effects) {
      Json item{{"definition", origin(definition.origin)}};
      if (definition.choose) {
        item["choose"] = *definition.choose;
        item["selection"] = definition.selection;
      } else {
        item["scopes"] = Json::object();
        for (size_t i = 0; i < kEffectScopeCount; ++i)
          if (definition.scopes[i])
            item["scopes"][kEffectScopeNames[i]] = pipeline(*definition.scopes[i]);
      }
      result["library"][name] = std::move(item);
    }
    bool found = filter.empty();
    auto matches = [&](std::string_view kind, std::string_view id) {
      const bool match = filter.empty() || (filter == kind && target == id);
      found |= match;
      return match;
    };
    for (const auto& output : server.outputs()) {
      const std::string id = output->wlr()->name;
      if (matches("--output", id)) {
        auto item = state(output->effectState(), kOutputEffects, false, [&](EffectScope scope) {
          return output->activeEffect(scope);
        });
        item["id"] = id;
        result["outputs"].push_back(std::move(item));
      }
      for (const auto& [name, region] : output->regionEffects()) {
        if (!matches("--region", name))
          continue;
        auto item = state(region, effectBit(EffectScope::Screen), false, [](EffectScope) -> const EffectEvent* {
          return nullptr;
        });
        item["name"] = name;
        item["output"] = id;
        result["regions"].push_back(std::move(item));
      }
    }
    for (const auto& view : server.views()) {
      if (!view->mapped())
        continue;
      const std::string id = view->extForeignIdentifier() ? view->extForeignIdentifier() : "";
      if (!matches("--window", id))
        continue;
      auto item = state(view->effectState(), kWindowEffects, false, [&](EffectScope scope) {
        return view->activeEffect(scope);
      });
      item["id"] = id;
      result["windows"].push_back(std::move(item));
    }
    for (const auto& layer : server.layerSurfaces()) {
      if (!layer->mapped() || !matches("--layer", layer->inspectionId()))
        continue;
      auto item = state(layer->effectState(), kLayerEffects, true, [&](EffectScope scope) {
        return layer->activeEffect(scope);
      });
      item["id"] = layer->inspectionId();
      result["layers"].push_back(std::move(item));
    }
    if (!found)
      return {{"err", "effect inspection target not found"}};
    return {{"ok", std::move(result)}};
  }
} // namespace umbriel
