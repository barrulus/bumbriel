#include "scene/window_presentation.h"

#include "output/output.h"
#include "scene/composition.h"
#include "scene/effect_registry.h"
#include "scene/window_sources.h"
#include "scene/workspace_presentation.h"
#include "scene/workspace_sources.h"
#include "scene/workspace_transition.h"
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"
#include "workspace/workspace.h"

extern "C" {
#include "../../umbrielfx/internal/render/fx_renderer/scene_program.h"
#include "../../umbrielfx/internal/types/wlr_scene.h"

#include <wlr/util/transform.h>
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace umbriel {
  namespace {
    std::array<float, 9> inputMatrix(enum wl_output_transform transform) {
      wlr_box origin{}, x{.x = 1, .y = 0, .width = 0, .height = 0};
      wlr_box y{.x = 0, .y = 1, .width = 0, .height = 0};
      transform = wlr_output_transform_invert(transform);
      wlr_box_transform(&origin, &origin, transform, 1, 1);
      wlr_box_transform(&x, &x, transform, 1, 1);
      wlr_box_transform(&y, &y, transform, 1, 1);
      return {static_cast<float>(x.x - origin.x), static_cast<float>(x.y - origin.y), 0,
              static_cast<float>(y.x - origin.x), static_cast<float>(y.y - origin.y), 0,
              static_cast<float>(origin.x),       static_cast<float>(origin.y),       1};
    }
    void boxFields(float out[4], const wlr_box& box, const wlr_box& output) {
      out[0] = static_cast<float>(box.x - output.x);
      out[1] = static_cast<float>(box.y - output.y);
      out[2] = static_cast<float>(box.width);
      out[3] = static_cast<float>(box.height);
    }
    bool inside(wlr_scene_node* node, wlr_scene_node* ancestor) {
      for (; node != nullptr; node = node->parent ? &node->parent->node : nullptr) {
        if (node == ancestor)
          return true;
      }
      return false;
    }
    struct RetainedWindowFrame final : PresentationVisualSource {
      struct Item {
        fx_scene_source_pair_for_test pair{};
        fx_scene_source_pair_for_test mask{};
        fx_scene_emission_source emission{};
        wlr_box extent{};
        fx_scene_draw draw{};
        fx_scene_shadow shadow{};
        ~Item() {
          fx_scene_source_pair_finish_for_test(&pair);
          fx_scene_source_pair_finish_for_test(&mask);
          fx_scene_emission_source_finish(&emission);
        }
      };
      fx_scene_reservation reservation{};
      std::vector<std::unique_ptr<Item>> items;
      ~RetainedWindowFrame() {
        items.clear();
        fx_scene_release(&reservation);
      }
    };
    struct WindowSourceHandle final : PresentationVisualSource {
      std::shared_ptr<const RetainedWindowFrame> frame;
    };
  } // namespace

  struct WindowPresentation::State {
    struct Owner {
      View* view = nullptr;
      wlr_scene_node* content = nullptr;
      wlr_scene_node* border = nullptr;
      wlr_scene_shadow* shadow = nullptr;
      wlr_box box{};
      View::SourceMotion motion{};
      std::vector<fx_scene_source_node_override> overrides;
      unsigned token = 0;
      float opacity = 1;
      std::array<float, 4> shadowColor{};
      float radius = 0;
      int borderWidth = 0;
    };
    struct Spec {
      wlr_scene_node* first = nullptr;
      wlr_scene_node* last = nullptr;
      Owner* owner = nullptr;
      int kind = FX_SCENE_STATIC;
      fx_scene_source_view view{};
      fx_scene_source_view_plan plan{};
      std::vector<fx_scene_source_node_override> overrides;
      std::vector<wlr_scene_tree*> clips;
      uint64_t emissionBytes = 0;
      uint64_t emissionRetained = 0;
      uint64_t emissionCapture = 0;
      bool coldEmission = false;
    };
    Server& server;
    Output& output;
    fx_scene_resource_pool pool{.limit = FX_SCENE_OUTPUT_BUDGET, .used = 0};
    PresentationLease lease;
    std::shared_ptr<const scene_experiment::ProgramBundle> bundle;
    std::unique_ptr<SceneComposition> composition;
    std::shared_ptr<RetainedWindowFrame> current;
    std::shared_ptr<RetainedWindowFrame> candidate;
    std::shared_ptr<WindowSourceHandle> sourceHandle;
    wl_event_source* preparationTimeout = nullptr;
    fx_scene_mesh mesh{};
    wlr_scene_tree* tree = nullptr;
    wlr_scene_buffer* picture = nullptr;
    wlr_box outputBox{};
    std::string target;
    std::string workspace;
    CloseSnapshotId snapshot = kInvalidCloseSnapshot;
    uint64_t identity = 0;
    uint64_t deadline = 0;
    uint64_t start = 0;
    uint64_t frames = 0;
    int targetToken = 0;
    bool pending = false;
    bool overlap = false;
    bool callbacksPending = false;
    bool renderLocked = false;
    bool inputOwned = false;
    bool companionsPrepared = false;
    bool workingSpace = false;
    bool floatingPoint = false;
    int width = 0, height = 0;
    float scale = 1;
    enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    PresentationFallback fallback = PresentationFallback::None;
    std::string admission;
    std::vector<std::string> participants;
    std::vector<wlr_scene_node*> order;

    fx_scene_source_view sourceView(wlr_scene_node* first, wlr_scene_node* last) const {
      fx_scene_source_view result{};
      result.first = first;
      result.last = last;
      result.extent = outputBox;
      result.viewport = outputBox;
      result.scale = scale;
      result.transparent = true;
      return result;
    }
    bool gather(std::vector<Owner>& owners, std::optional<WindowCloseSource>& closing) {
      owners.reserve(33);
      std::vector<std::string> ids;
      for (const auto& view : server.views()) {
        if (!view->mapped()
            || !view->animatesOn(&output)
            || (!view->onActiveWorkspace() && !view->pinned())
            || !view->sceneTree()->node.enabled)
          continue;
        if (owners.size() >= 32) {
          admission = "participant_limit";
          return false;
        }
        const auto supported = fx_scene_participant_admit_for_test(&view->sceneTree()->node);
        if (supported != FX_SCENE_PARTICIPANT_SUPPORTED) {
          admission = supported == FX_SCENE_PARTICIPANT_BLUR ? "backdrop_blur"
              : supported == FX_SCENE_PARTICIPANT_IN_PLACE   ? "in_place_effect"
                                                             : "unsupported_topology";
          return false;
        }
        const std::string id = view->extForeignIdentifier() ? view->extForeignIdentifier() : "";
        ids.push_back(id);
        auto& owner = owners.emplace_back();
        owner.view = view.get();
        owner.content = &view->sourceContentTree()->node;
        owner.border = view->sourceBorderTree() ? &view->sourceBorderTree()->node : nullptr;
        owner.shadow = const_cast<wlr_scene_shadow*>(view->shadowNode());
        owner.box = view->presentedBox();
        owner.motion = view->sourceMotion();
        owner.opacity = view->presentedOpacity();
        owner.radius = static_cast<float>(view->decorationCornerRadius());
        owner.borderWidth = view->decorationBorderWidth() + view->decorationOuterBorderWidth();
        owner.token = static_cast<unsigned>(owners.size());
        if (owner.shadow)
          std::ranges::copy(owner.shadow->color, owner.shadowColor.begin());
        if (snapshot == 0 && id == target) {
          owner.overrides =
              view->sourceOverridesWithoutLifecycle(1U << static_cast<unsigned>(AnimationEvent::WindowsIn));
          owner.opacity = view->sourceOpacityWithoutLifecycle();
          for (const auto& item : owner.overrides)
            if (owner.shadow && item.node == &owner.shadow->node && item.has_colors) {
              std::ranges::copy(item.colors[0], owner.shadowColor.begin());
            }
        }
      }
      std::ranges::sort(ids);
      if (!participants.empty() && participants != ids) {
        admission = "participant_topology";
        return false;
      }
      participants = std::move(ids);
      if (snapshot != 0) {
        closing = server.closeSceneSource(snapshot);
        if (!closing)
          return false;
        if (owners.size() >= 32) {
          admission = "participant_limit";
          return false;
        }
        auto& owner = owners.emplace_back();
        owner.content = &closing->tree->node;
        owner.shadow = closing->shadow;
        owner.box = closing->box;
        owner.motion = {.source = owner.box, .destination = owner.box};
        owner.overrides = closing->overrides;
        owner.borderWidth = closing->borderWidth;
        owner.radius = static_cast<float>(closing->cornerRadius);
        for (const auto& item : owner.overrides)
          if (owner.shadow && item.node == &owner.shadow->node && item.has_colors) {
            std::ranges::copy(item.colors[0], owner.shadowColor.begin());
          }
        owner.token = static_cast<unsigned>(owners.size());
      }
      return !owners.empty();
    }
    bool specs(std::vector<Owner>& owners, std::vector<Spec>& result) {
      std::unordered_map<wlr_scene_node*, Owner*> content, shadow, border;
      for (auto& owner : owners) {
        content.emplace(owner.content, &owner);
        if (owner.shadow && owner.shadow->node.enabled)
          shadow.emplace(&owner.shadow->node, &owner);
        if (owner.border)
          border.emplace(owner.border, &owner);
      }
      auto* lightLayer = fx_scene_source_light_layer(server.scene());
      const auto recognized = [&](wlr_scene_node* node) {
        return content.contains(node) || shadow.contains(node) || (lightLayer && node == &lightLayer->node);
      };
      const auto contains = [&](auto&& self, wlr_scene_node* node) -> bool {
        if (recognized(node))
          return true;
        if (node->type != WLR_SCENE_NODE_TREE)
          return false;
        wlr_scene_node* child;
        wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
          if (child->enabled && self(self, child))
            return true;
        }
        return false;
      };
      const auto add = [&](wlr_scene_node* first, wlr_scene_node* last, Owner* owner, int kind) {
        auto& spec = result.emplace_back();
        spec.first = first;
        spec.last = last;
        spec.owner = owner;
        spec.kind = kind;
      };
      std::unordered_set<Owner*> belowLight;
      const auto walk = [&](auto&& self, wlr_scene_tree* parent) -> void {
        wlr_scene_node* run = nullptr;
        wlr_scene_node* end = nullptr;
        const auto flush = [&] {
          if (run)
            add(run, end, nullptr, FX_SCENE_STATIC);
          run = end = nullptr;
        };
        wlr_scene_node* node;
        wl_list_for_each(node, &parent->children, link) {
          if (!node->enabled || node == (tree ? &tree->node : nullptr)) {
            flush();
            continue;
          }
          bool foreign = false;
          for (const auto& other : server.outputs())
            if (other.get() != &output
                && (node == &other->viewRoot()->node
                    || node == &other->fullscreenRoot()->node
                    || node == &other->pinnedRoot()->node))
              foreign = true;
          if (foreign) {
            flush();
            continue;
          }
          if (content.contains(node)) {
            flush();
            add(node, node, content.at(node), FX_SCENE_CONTENT);
            belowLight.insert(content.at(node));
          } else if (shadow.contains(node)) {
            flush();
            add(node, node, shadow.at(node), FX_SCENE_SHADOW);
          } else if (lightLayer && node == &lightLayer->node) {
            flush();
            std::unordered_set<Owner*> emitted;
            wlr_scene_node* occurrence;
            wl_list_for_each(occurrence, &lightLayer->children, link) {
              auto* source = fx_scene_source_light_owner(occurrence);
              if (border.contains(source)) {
                auto* owner = border.at(source);
                add(owner->content, owner->content, owner, FX_SCENE_EMISSION);
                emitted.insert(owner);
              }
            }
            for (auto& owner : owners)
              if (owner.border
                  && belowLight.contains(&owner)
                  && !emitted.contains(&owner)
                  && wlr_scene_node_effect_requirements(owner.border).light) {
                // A missing cold cache is not permission to omit illumination.
                // The source query below must explicitly admit or reject it.
                add(owner.content, owner.content, &owner, FX_SCENE_EMISSION);
              }
          } else if (contains(contains, node)) {
            flush();
            self(self, wlr_scene_tree_from_node(node));
          } else {
            if (!run)
              run = node;
            end = node;
          }
          if (parent == &server.scene()->tree && node == &server.pinnedTree()->node)
            break;
        }
        flush();
      };
      walk(walk, &server.scene()->tree);
      if (result.size() > FX_SCENE_MAX_DRAWS) {
        admission = "draw_limit";
        return false;
      }
      std::vector<wlr_scene_node*> currentOrder;
      for (auto& spec : result) {
        if (spec.owner && spec.kind != FX_SCENE_STATIC)
          currentOrder.push_back(spec.first);
        spec.view = sourceView(spec.first, spec.last);
        if (spec.owner)
          for (const auto& override : spec.owner->overrides) {
            if (inside(override.node, spec.first))
              spec.overrides.push_back(override);
          }
        if (spec.owner) {
          spec.clips = {output.viewRoot(), output.fullscreenRoot()};
          auto* workspace = spec.owner->view ? spec.owner->view->workspace() : output.workspaceGroup()->active();
          if (workspace) {
            spec.clips.push_back(workspace->tileShadowLayer()->node.parent);
            spec.clips.push_back(workspace->fullscreenTree());
          }
          spec.view.bypass_clips = spec.clips.data();
          spec.view.bypass_clip_count = spec.clips.size();
        }
        spec.view.nodes = spec.overrides.data();
        spec.view.node_count = spec.overrides.size();
        if (spec.kind == FX_SCENE_EMISSION) {
          spec.emissionBytes = fx_scene_emission_source_bytes(output.sceneOutput(), spec.owner->border);
          spec.emissionRetained = spec.emissionBytes;
          if (!spec.emissionBytes) {
            spec.coldEmission = true;
            struct fx_scene_emission_view_plan plan{};
            if (fx_scene_emission_view_plan(output.sceneOutput(), &spec.view, spec.owner->border, &plan)) {
              spec.emissionBytes = plan.total_bytes;
              spec.emissionRetained = plan.retained_bytes;
              spec.emissionCapture = plan.capture_bytes;
            }
          }
          if (!spec.emissionBytes) {
            admission = "unavailable_emission";
            return false;
          }
          continue;
        }
        wlr_box bounds{};
        if (!fx_scene_source_view_bounds_for_test(output.sceneOutput(), &spec.view, FX_SCENE_SOURCE_CONTENT, &bounds))
          return false;
        if (spec.kind == FX_SCENE_STATIC) {
          wlr_box_intersection(&bounds, &bounds, &outputBox);
        }
        if (bounds.width <= 0 || bounds.height <= 0) {
          spec.first = nullptr;
          continue;
        }
        spec.view.extent = bounds;
        if (!fx_scene_source_view_plan_for_test(output.sceneOutput(), &spec.view, &spec.plan))
          return false;
        if (spec.plan.history_bytes != 0) {
          admission = "feedback_effect";
          return false;
        }
      }
      // Ordinary layout can restack the same live roots during a resize.
      // Rebuild their draw order from the native scene, but reject changes to
      // the admitted participant node inventory (including companion counts).
      if (!order.empty() && !std::ranges::is_permutation(order, currentOrder)) {
        admission = "stacking_changed";
        return false;
      }
      order = std::move(currentOrder);
      std::erase_if(result, [](const Spec& spec) { return spec.first == nullptr; });
      return !result.empty();
    }
    bool capture(std::vector<Spec>& specs, const fx_scene_frame& frame) {
      uint64_t bytes = 0, peak = 0;
      for (const auto& spec : specs) {
        const uint64_t retained = spec.kind == FX_SCENE_EMISSION ? spec.emissionRetained : spec.plan.retained_bytes;
        if (bytes > std::numeric_limits<uint64_t>::max() - retained)
          return false;
        bytes += retained;
        peak = std::max({peak, spec.plan.capture_bytes, spec.emissionCapture});
        if (spec.kind == FX_SCENE_SHADOW) {
          // One independent opaque geometry image; the roles alias it.
          const uint64_t maskBytes = (static_cast<uint64_t>(spec.plan.width) * (floatingPoint ? 8 : 4) + 255)
                  * static_cast<uint64_t>(spec.plan.height)
              + 4096;
          if (bytes > std::numeric_limits<uint64_t>::max() - maskBytes)
            return false;
          bytes += maskBytes;
        }
      }
      auto next = std::make_shared<RetainedWindowFrame>();
      if (bytes > std::numeric_limits<uint64_t>::max() - peak
          || !fx_scene_reserve(&next->reservation, &pool, &presentationAggregatePool(), bytes + peak)) {
        fallback = PresentationFallback::ResourceBudget;
        admission = "source_reservation_"
            + std::to_string(bytes)
            + "_peak_"
            + std::to_string(peak)
            + "_used_"
            + std::to_string(pool.used);
        return false;
      }
      std::vector<fx_effect_light> lights;
      unsigned shadowPadding = 0, lightPadding = 0;
      for (auto& spec : specs) {
        auto& item = *next->items.emplace_back(std::make_unique<RetainedWindowFrame::Item>());
        item.extent = spec.view.extent;
        if (spec.kind == FX_SCENE_EMISSION) {
          const bool ok = spec.coldEmission
              ? fx_scene_emission_view_capture(
                    output.sceneOutput(), &spec.view, spec.owner->border, spec.emissionBytes, &item.emission
                )
              : fx_scene_emission_source_capture(
                    output.sceneOutput(), spec.owner->border, spec.emissionBytes, &item.emission
                );
          if (!ok) {
            admission = "emission_capture";
            return false;
          }
          item.extent = item.emission.extent;
          item.draw.emission = true;
          item.draw.light = &item.emission.recipe;
          lights.push_back(item.emission.recipe);
          lightPadding = std::max(
              lightPadding, static_cast<unsigned>(std::ceil(std::ceil(item.emission.recipe.spread * 2 + 8) * scale))
          );
        } else {
          if (!fx_scene_source_view_pair_capture_for_test(
                  output.sceneOutput(), &spec.view, spec.plan.total_bytes, &item.pair
              )) {
            admission = "source_capture_" + std::to_string(spec.kind);
            return false;
          }
          if (spec.kind == FX_SCENE_SHADOW) {
            item.mask.working_space = workingSpace;
            item.mask.floating_point = floatingPoint;
            item.mask.display = fx_scene_buffer_create(
                server.renderer(), server.allocator(), spec.plan.width, spec.plan.height, floatingPoint
            );
            item.mask.unfiltered = item.mask.display;
            if (!item.mask.display) {
              admission = "shadow_mask_allocation";
              return false;
            }
            auto* target = fx_scene_target_create_with_color(server.renderer(), item.mask.display, false, workingSpace);
            if (!target) {
              admission = "shadow_mask_target";
              return false;
            }
            auto maskFrame = frame;
            maskFrame.output_size[0] = static_cast<float>(item.extent.width);
            maskFrame.output_size[1] = static_cast<float>(item.extent.height);
            auto geometry = spec.owner->box;
            geometry.x -= spec.owner->borderWidth;
            geometry.y -= spec.owner->borderWidth;
            geometry.width += 2 * spec.owner->borderWidth;
            geometry.height += 2 * spec.owner->borderWidth;
            float maskBox[4];
            boxFields(maskBox, geometry, item.extent);
            const float radius = spec.owner->radius + static_cast<float>(spec.owner->borderWidth);
            const float corners[4] = {radius, radius, radius, radius};
            const bool masked = fx_scene_target_mask(target, &maskFrame, maskBox, corners);
            fx_scene_target_destroy(target);
            if (!masked) {
              admission = "shadow_mask_render";
              return false;
            }
            const auto& cfg = config().appearance.shadow;
            item.shadow.softness = static_cast<float>(cfg.softness) * scale;
            item.shadow.offset[0] = static_cast<float>(cfg.offsetX) * scale;
            item.shadow.offset[1] = static_cast<float>(cfg.offsetY) * scale;
            std::ranges::copy(spec.owner->shadowColor, item.shadow.color);
            wlr_box physical{
                static_cast<int>(std::lround((item.extent.x - outputBox.x) * scale)),
                static_cast<int>(std::lround((item.extent.y - outputBox.y) * scale)),
                static_cast<int>(std::lround(item.extent.width * scale)),
                static_cast<int>(std::lround(item.extent.height * scale))
            };
            int logicalWidth = width, logicalHeight = height;
            if (transform & 1)
              std::swap(logicalWidth, logicalHeight);
            wlr_box_transform(&physical, &physical, transform, logicalWidth, logicalHeight);
            item.shadow.native_box[0] = physical.x;
            item.shadow.native_box[1] = physical.y;
            item.shadow.native_box[2] = physical.width;
            item.shadow.native_box[3] = physical.height;
            const float p = std::clamp(frame.progress, 0.0F, 1.0F);
            item.shadow.native_mix = std::pow(1 - 4 * p * (1 - p), 4.0F);
            item.draw.shadow = &item.shadow;
            shadowPadding = std::max(
                shadowPadding,
                static_cast<unsigned>(std::ceil(
                    item.shadow.softness * 3 + std::abs(item.shadow.offset[0]) + std::abs(item.shadow.offset[1]) + 2
                ))
            );
          }
        }
        item.draw.mesh = &mesh;
        item.draw.item.kind = spec.kind;
        item.draw.item.ordinal = static_cast<int>(next->items.size() - 1);
        item.draw.item.token = spec.owner ? static_cast<int>(spec.owner->token) : 0;
        item.draw.item.native_opacity = spec.owner ? spec.owner->opacity : 1;
        const auto& content = spec.owner ? spec.owner->box : item.extent;
        boxFields(item.draw.item.current_box, content, outputBox);
        boxFields(item.draw.item.capture_extent, item.extent, outputBox);
        boxFields(item.draw.item.content_bounds, content, outputBox);
        boxFields(item.draw.item.source_box, spec.owner ? spec.owner->motion.source : content, outputBox);
        boxFields(item.draw.item.destination_box, spec.owner ? spec.owner->motion.destination : content, outputBox);
        item.draw.item.motion_progress = spec.owner ? spec.owner->motion.progress : 1;
        item.draw.item.linear_motion_progress = spec.owner ? spec.owner->motion.linearProgress : 1;
        boxFields(item.draw.item.coverage_box, content, outputBox);
        item.draw.item.framing_transform[0] = item.draw.item.framing_transform[1] = 1;
      }
      if (!companionsPrepared) {
        if (!composition->prepareCompanions(shadowPadding, lightPadding, lights, scale)) {
          fallback = PresentationFallback::ResourceBudget;
          admission = "companion_preparation_used_" + std::to_string(pool.used);
          return false;
        }
        companionsPrepared = true;
      }
      fx_scene_release(&next->reservation);
      if (!fx_scene_reserve(&next->reservation, &pool, &presentationAggregatePool(), bytes))
        return false;
      candidate = std::move(next);
      return true;
    }
  };

  WindowPresentation::WindowPresentation(Server& server, Output& output)
      : m_state(std::make_unique<State>(server, output)) {}
  WindowPresentation::~WindowPresentation() { cancel(PresentationFallback::OutputRemoved); }
  void WindowPresentation::opening(View& view) {
    auto& state = *m_state;
    if (active()) {
      state.overlap = true;
      cancel(PresentationFallback::OverlappingLifecycle);
      return;
    }
    if (state.overlap && !state.server.nativePresentationLifecycles(&state.output).empty())
      return;
    state.overlap = false;
    if ((state.output.workspacePresentation() && state.output.workspacePresentation()->active())
        || (state.output.workspaceTransition() && state.output.workspaceTransition()->active())) {
      state.admission = "competing_presentation";
      state.fallback = PresentationFallback::UnsupportedCapability;
      return;
    }
    auto bundle = state.server.effects().sceneAnimationEffect(AnimationEvent::WindowsIn);
    if (!bundle || !view.onActiveWorkspace() || view.extForeignIdentifier() == nullptr)
      return;
    state.bundle = std::move(bundle);
    state.target = view.extForeignIdentifier();
    state.snapshot = 0;
    state.pending = true;
    state.preparationTimeout = wl_event_loop_add_timer(
        wl_display_get_event_loop(state.server.display()),
        [](void* data) {
          static_cast<WindowPresentation*>(data)->cancel(PresentationFallback::SourceUnavailable);
          return 0;
        },
        this
    );
    if (state.preparationTimeout == nullptr || wl_event_source_timer_update(state.preparationTimeout, 2000) != 0) {
      cancel(PresentationFallback::SourceUnavailable);
      return;
    }
    state.fallback = PresentationFallback::None;
    state.admission.clear();
  }
  void WindowPresentation::closing(View&, CloseSnapshotId snapshot) {
    auto& state = *m_state;
    if (active()) {
      state.overlap = true;
      cancel(PresentationFallback::OverlappingLifecycle);
      return;
    }
    if (state.overlap && !state.server.nativePresentationLifecycles(&state.output).empty())
      return;
    state.overlap = false;
    if ((state.output.workspacePresentation() && state.output.workspacePresentation()->active())
        || (state.output.workspaceTransition() && state.output.workspaceTransition()->active())) {
      state.admission = "competing_presentation";
      state.fallback = PresentationFallback::UnsupportedCapability;
      return;
    }
    auto bundle = state.server.effects().sceneAnimationEffect(AnimationEvent::WindowsOut);
    if (!bundle || snapshot == kInvalidCloseSnapshot)
      return;
    state.bundle = std::move(bundle);
    state.target.clear();
    state.snapshot = snapshot;
    state.pending = true;
    state.preparationTimeout = wl_event_loop_add_timer(
        wl_display_get_event_loop(state.server.display()),
        [](void* data) {
          static_cast<WindowPresentation*>(data)->cancel(PresentationFallback::SourceUnavailable);
          return 0;
        },
        this
    );
    if (state.preparationTimeout == nullptr || wl_event_source_timer_update(state.preparationTimeout, 2000) != 0) {
      cancel(PresentationFallback::SourceUnavailable);
      return;
    }
    state.fallback = PresentationFallback::None;
    state.admission.clear();
  }
  void WindowPresentation::topologyChanged() {
    if (active()) {
      m_state->overlap = true;
      cancel(PresentationFallback::TopologyChanged);
    }
  }
  void WindowPresentation::prepareFrame(bool) {
    auto& state = *m_state;
    if (!active()) {
      if (state.overlap && state.server.nativePresentationLifecycles(&state.output).empty())
        state.overlap = false;
      return;
    }
    if (state.server.sessionLocked()) {
      cancel(PresentationFallback::Locked);
      return;
    }
    const auto event = state.snapshot ? AnimationEvent::WindowsOut : AnimationEvent::WindowsIn;
    const auto binding = config().animation.eventEffect(event);
    if (!config().animation.enabled
        || !binding.enabled
        || !binding.effect
        || *binding.effect != state.bundle->definition.name) {
      cancel(PresentationFallback::BindingRemoved);
      return;
    }
    const auto lifecycles = state.server.nativePresentationLifecycles(&state.output);
    if (lifecycles.size() > 1) {
      state.overlap = true;
      cancel(PresentationFallback::OverlappingLifecycle);
      return;
    }
    View* target = nullptr;
    AnimatedValue lifecycle;
    if (state.snapshot) {
      auto source = state.server.closeSceneSource(state.snapshot);
      if (!source) {
        cancel(PresentationFallback::None);
        return;
      }
      lifecycle = source->lifecycle;
    } else {
      for (const auto& view : state.server.views())
        if (view->mapped() && view->extForeignIdentifier() && state.target == view->extForeignIdentifier())
          target = view.get();
      if (!target) {
        cancel(PresentationFallback::TopologyChanged);
        return;
      }
      lifecycle = target->nativeLifecycle();
    }
    if (!lifecycle.animating() || lifecycle.startMsec() == 0) {
      if (state.lease.active() || (target && lifecycle.current() >= 1))
        cancel(PresentationFallback::None);
      return;
    }
    if (state.lease.active()
        && (state.server.animationClockMsec() >= state.deadline || lifecycle.transitionId() != state.identity)) {
      cancel(PresentationFallback::None);
      return;
    }
    if (state.composition && state.composition->pending())
      return;
    const auto box = state.output.layoutBox();
    if (!state.composition) {
      state.outputBox = box;
      state.width = state.output.wlr()->width;
      state.height = state.output.wlr()->height;
      state.scale = state.output.wlr()->scale;
      state.transform = state.output.wlr()->transform;
      state.workingSpace = fx_scene_source_working_space(state.output.sceneOutput());
      state.floatingPoint = fx_scene_source_floating_point(state.output.sceneOutput());
      state.workspace = state.output.workspaceGroup()->active()->id();
      state.composition = SceneComposition::create(
          state.server.renderer(), state.server.allocator(), state.pool, presentationAggregatePool(), state.bundle,
          state.width, state.height, state.workingSpace, state.floatingPoint
      );
      if (!state.composition || !fx_scene_mesh_create(&state.mesh, 12, 12, FX_SCENE_MAX_DRAWS)) {
        cancel(PresentationFallback::ResourceBudget);
        return;
      }
    } else if (
        !wlr_box_equal(&box, &state.outputBox)
        || state.output.wlr()->transform != state.transform
        || state.output.wlr()->scale != state.scale
        || state.output.workspaceGroup()->active()->id() != state.workspace
        || fx_scene_source_working_space(state.output.sceneOutput()) != state.workingSpace
        || fx_scene_source_floating_point(state.output.sceneOutput()) != state.floatingPoint
    ) {
      cancel(PresentationFallback::TopologyChanged);
      return;
    }
    std::vector<State::Owner> owners;
    std::optional<WindowCloseSource> closing;
    if (!state.gather(owners, closing)) {
      cancel(PresentationFallback::UnsupportedCapability);
      return;
    }
    for (const auto& owner : owners)
      if (owner.view) {
        auto* surface = owner.view->toplevel()->base;
        if (surface->configure_idle || surface->current.configure_serial != surface->scheduled_serial)
          return;
      }
    std::vector<State::Spec> specs;
    if (!state.specs(owners, specs)) {
      cancel(PresentationFallback::UnsupportedCapability);
      return;
    }
    fx_scene_frame frame{};
    frame.output_size[0] = static_cast<float>(box.width);
    frame.output_size[1] = static_cast<float>(box.height);
    frame.scale = state.scale;
    frame.output_transform = state.transform;
    frame.time = state.output.effectSeconds();
    frame.progress = static_cast<float>(state.snapshot ? 1 - lifecycle.current() : lifecycle.current());
    frame.linear_progress = static_cast<float>(lifecycle.progress());
    frame.direction = state.snapshot ? -1 : 1;
    frame.scene_count = static_cast<int>(owners.size());
    frame.viewport[2] = frame.output_size[0];
    frame.viewport[3] = frame.output_size[1];
    std::ranges::copy(lifecycle.shaderSeed(), frame.random_seed);
    for (const auto& owner : owners)
      if ((target && owner.view == target) || (state.snapshot && !owner.view))
        frame.target_token = static_cast<int>(owner.token);
    state.targetToken = frame.target_token;
    std::vector<View*> contributing;
    contributing.reserve(owners.size());
    for (const auto& owner : owners)
      if (owner.view)
        contributing.push_back(owner.view);
    state.server.effects().setSourceOccurrences(this, &state.output, contributing, true);
    state.server.effects().bindSourceAudio(this);
    state.server.effects().fillSceneAudio(frame, *state.bundle, &state.output);
    state.server.effects().fillScenePalette(frame, *state.bundle);
    if (!state.capture(specs, frame)) {
      cancel(
          state.fallback == PresentationFallback::ResourceBudget ? state.fallback
                                                                 : PresentationFallback::SourceUnavailable
      );
      return;
    }
    const auto matrix = inputMatrix(state.transform);
    std::vector<SceneComposition::Source> inputs;
    std::vector<fx_scene_draw> draws;
    for (const auto& item : state.candidate->items) {
      SceneComposition::Source source;
      if (item->draw.emission) {
        source.display = item->emission.display;
        source.unfiltered = item->emission.unfiltered;
      } else if (item->draw.shadow) {
        source.display = item->mask.display;
        source.unfiltered = item->mask.unfiltered;
        source.nativeShadowDisplay = item->pair.display;
        source.nativeShadowUnfiltered = item->pair.unfiltered;
      } else {
        source.display = item->pair.display;
        source.unfiltered = item->pair.unfiltered;
      }
      source.sampleMatrix = matrix.data();
      inputs.push_back(source);
      draws.push_back(item->draw);
    }
    if (!state.composition->render(frame, inputs, draws)) {
      cancel(PresentationFallback::CompositionFailure);
      return;
    }
    if (!state.lease.active()) {
      PresentationRequest request;
      request.scope = PresentationScope::WindowScene;
      request.identity = lifecycle.transitionId();
      request.startMsec = lifecycle.startMsec();
      request.deadlineMsec = request.startMsec + lifecycle.durationMs();
      state.sourceHandle = std::make_shared<WindowSourceHandle>();
      state.sourceHandle->frame = state.candidate;
      request.sources.push_back({state.sourceHandle, state.sourceHandle});
      state.inputOwned = state.output.beginSceneInput([this] { cancel(PresentationFallback::InputDismissal); });
      if (!state.inputOwned || !state.lease.acquire(std::move(request))) {
        cancel(PresentationFallback::InputGrab);
        return;
      }
      if (state.preparationTimeout) {
        wl_event_source_remove(state.preparationTimeout);
        state.preparationTimeout = nullptr;
      }
      state.identity = lifecycle.transitionId();
      state.start = lifecycle.startMsec();
      state.deadline = state.start + lifecycle.durationMs();
      wlr_output_lock_attach_render(state.output.wlr(), true);
      state.renderLocked = true;
      state.server.effects().updateSceneAudio(this, &state.output, state.bundle.get(), true);
    }
    const auto image = state.composition->candidate();
    if (!state.tree) {
      state.tree = wlr_scene_tree_create(&state.server.scene()->tree);
      if (!state.tree) {
        cancel(PresentationFallback::CompositionFailure);
        return;
      }
      wlr_scene_node_set_enabled(&state.tree->node, false);
      wlr_scene_node_place_above(&state.tree->node, &state.server.pinnedTree()->node);
      wlr_scene_node_set_position(&state.tree->node, box.x, box.y);
      state.picture = wlr_scene_buffer_create(state.tree, image.display);
      if (!state.picture) {
        cancel(PresentationFallback::CompositionFailure);
        return;
      }
      wlr_scene_buffer_set_dest_size(state.picture, box.width, box.height);
      wlr_scene_buffer_set_transform(state.picture, state.transform);
      if (state.workingSpace) {
        wlr_scene_buffer_set_transfer_function(state.picture, WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR);
        wlr_scene_buffer_set_primaries(state.picture, WLR_COLOR_NAMED_PRIMARIES_SRGB);
      }
    } else
      wlr_scene_buffer_set_buffer(state.picture, image.display);
    wlr_scene_node* first;
    first = wl_container_of(state.server.scene()->tree.children.next, first, link);
    if (!fx_scene_output_replace_range_for_test(state.output.sceneOutput(), first, &state.server.pinnedTree()->node)
        || !fx_scene_output_bind_replacement_roles_for_test(
            state.output.sceneOutput(), state.picture, image.unfiltered
        )) {
      cancel(PresentationFallback::CompositionFailure);
      return;
    }
    wlr_scene_node_set_enabled(&state.tree->node, true);
    state.pending = false;
  }
  void WindowPresentation::frameSubmitted(bool success) {
    auto& state = *m_state;
    if (!state.composition || !state.composition->pending())
      return;
    state.composition->submitted(success);
    if (success) {
      state.current = std::move(state.candidate);
      state.sourceHandle->frame = state.current;
      state.callbacksPending = true;
      ++state.frames;
    }
  }
  void WindowPresentation::sendFrameDone(const timespec& when) {
    auto& state = *m_state;
    if (!state.callbacksPending || !state.lease.active())
      return;
    state.callbacksPending = false;
    wlr_scene_frame_done_event event{.output = state.output.sceneOutput(), .when = when};
    wlr_scene_node* node;
    wl_list_for_each(node, &state.server.scene()->tree.children, link) {
      wlr_scene_node_for_each_buffer(
          node,
          [](wlr_scene_buffer* buffer, int, int, void* data) {
            wlr_scene_buffer_send_frame_done(buffer, static_cast<wlr_scene_frame_done_event*>(data));
          },
          &event
      );
      if (node == &state.server.pinnedTree()->node)
        break;
    }
  }
  void WindowPresentation::cancel(PresentationFallback reason) {
    auto& state = *m_state;
    const bool visible = state.tree != nullptr;
    state.pending = false;
    if (state.preparationTimeout) {
      wl_event_source_remove(state.preparationTimeout);
      state.preparationTimeout = nullptr;
    }
    state.fallback = reason;
    state.lease.cancel(reason);
    state.server.effects().clearSceneAudio(this);
    state.server.effects().clearSourceOccurrences(this);
    if (state.tree) {
      wlr_scene_node_set_enabled(&state.tree->node, false);
      fx_scene_output_replace_range_for_test(state.output.sceneOutput(), nullptr, nullptr);
      wlr_scene_node_destroy(&state.tree->node);
      state.tree = nullptr;
      state.picture = nullptr;
    }
    state.composition.reset();
    state.candidate.reset();
    state.current.reset();
    state.sourceHandle.reset();
    fx_scene_mesh_finish(&state.mesh);
    state.participants.clear();
    state.order.clear();
    state.companionsPrepared = false;
    if (state.renderLocked) {
      wlr_output_lock_attach_render(state.output.wlr(), false);
      state.renderLocked = false;
    }
    if (state.inputOwned) {
      state.output.endSceneInput(visible);
      state.inputOwned = false;
    }
    if (visible)
      wlr_output_schedule_frame(state.output.wlr());
  }
  bool WindowPresentation::active() const { return m_state->pending || m_state->lease.active(); }
  bool WindowPresentation::renderLocked() const { return m_state->renderLocked; }
  nlohmann::json WindowPresentation::status() const {
    const auto& state = *m_state;
    const auto fallback = [&] {
      switch (state.fallback) {
      case PresentationFallback::None:
        return "";
      case PresentationFallback::UnsupportedCapability:
        return "unsupported_capability";
      case PresentationFallback::SourceUnavailable:
        return "source_unavailable";
      case PresentationFallback::ResourceBudget:
        return "resource_budget";
      case PresentationFallback::CompositionFailure:
        return "composition_failure";
      case PresentationFallback::InputGrab:
        return "input_grab";
      case PresentationFallback::OverlappingLifecycle:
        return "overlapping_lifecycle";
      case PresentationFallback::TopologyChanged:
        return "topology_changed";
      case PresentationFallback::RendererLost:
        return "renderer_lost";
      case PresentationFallback::OutputRemoved:
        return "output_removed";
      case PresentationFallback::Locked:
        return "locked";
      case PresentationFallback::BindingRemoved:
        return "binding_removed";
      case PresentationFallback::InputDismissal:
        return "input_dismissal";
      }
      return "unknown";
    };
    auto native = nlohmann::json::array();
    for (const auto& lifecycle : state.server.nativePresentationLifecycles(&state.output)) {
      native.push_back(
          {{"identity", lifecycle.identity},
           {"snapshot", lifecycle.snapshot},
           {"start_msec", lifecycle.startMsec},
           {"deadline_msec", lifecycle.deadlineMsec}}
      );
    }
    auto views = nlohmann::json::array();
    for (const auto& view : state.server.views()) {
      if (!view->mapped() || !view->animatesOn(&state.output))
        continue;
      const auto box = view->presentedBox();
      views.push_back(
          {{"id", view->extForeignIdentifier() ? view->extForeignIdentifier() : ""},
           {"title", view->toplevel()->title ? view->toplevel()->title : ""},
           {"floating", view->floating()},
           {"box", {box.x, box.y, box.width, box.height}}}
      );
    }
    auto items = nlohmann::json::array();
    const auto& presented = state.current ? state.current : state.candidate;
    if (presented) {
      for (const auto& source : presented->items) {
        const auto& item = source->draw.item;
        const auto box = [](const float* value) { return std::array{value[0], value[1], value[2], value[3]}; };
        items.push_back(
            {{"kind", item.kind},
             {"token", item.token},
             {"current_box", box(item.current_box)},
             {"source_box", box(item.source_box)},
             {"destination_box", box(item.destination_box)},
             {"capture_extent", box(item.capture_extent)},
             {"motion_progress", item.motion_progress},
             {"linear_motion", item.linear_motion_progress}}
        );
      }
    }
    return {{"active", active()},          {"pending", state.pending},        {"overlap", state.overlap},
            {"identity", state.identity},  {"snapshot", state.snapshot},      {"target_token", state.targetToken},
            {"start_msec", state.start},   {"deadline_msec", state.deadline}, {"memory_bytes", state.pool.used},
            {"frames", state.frames},      {"admission", state.admission},    {"items", std::move(items)},
            {"native", std::move(native)}, {"views", std::move(views)},       {"fallback", fallback()}};
  }
} // namespace umbriel
