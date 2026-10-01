#include "scene/workspace_transition.h"

#include "config/config.h"
#include "output/output.h"
#include "overview/overview.h"
#include "scene/composition.h"
#include "scene/effect_registry.h"
#include "scene/workspace_sources.h"
#include "server/server.h"
#include "wlr.h"
#include "workspace/workspace.h"
extern "C" {
#include "../../umbrielfx/internal/render/fx_renderer/scene_program.h"
#include "../../umbrielfx/internal/types/scene_source.h"
#include "../../umbrielfx/internal/types/wlr_scene.h"

#include <wlr/util/transform.h>
}
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace umbriel {
  void Server::cancelScenePresentations(PresentationFallback reason) {
    for (const auto& output : m_outputs) {
      if (auto* transition = output->workspaceTransition()) {
        transition->cancel(reason);
      }
    }
  }

  bool Output::beginWorkspaceTransition(std::string_view destination, bool interactive) {
    if (!m_workspaceTransition) {
      m_workspaceTransition = std::make_unique<WorkspaceTransition>(*m_server, *this);
    }
    return m_workspaceTransition->begin(destination, interactive);
  }
  namespace {
    std::array<float, 9> sourceMatrix(wl_output_transform transform) {
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
  } // namespace

  struct WorkspaceTransition::State {
    Server& server;
    Output& output;
    std::shared_ptr<const scene_experiment::ProgramBundle> bundle;
    std::unique_ptr<WorkspaceSources> sources;
    std::unique_ptr<SceneComposition> composition;
    bool sourceReady = false;
    AnimatedValue progress;
    std::array<float, 4> seed{};
    uint64_t identity = 0;
    wlr_scene_tree* tree = nullptr;
    wlr_scene_buffer* picture = nullptr;
    wlr_box box{};
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    float scale = 1;
    int width = 0, height = 0;
    std::string original, destination;
    bool active = false, interactive = false, released = false, commit = true;
    bool workingSpace = false, pendingEnd = false, activated = false;
    bool floatingPoint = false;
    bool awaitNative = false;
    float direction = 1;
    bool horizontal = false;
    uint64_t lastSources = 0;
    double lastProgress = -1;
    float lastTime = 0;
    std::array<float, 16> lastPalette{};
    int lastPaletteCount = 0;
    PresentationFallback fallback = PresentationFallback::None;
    State(Server& server, Output& output) : server(server), output(output) {}
    bool install(SceneComposition::Source pair) {
      if (tree == nullptr) {
        tree = wlr_scene_tree_create(&server.scene()->tree);
        if (tree == nullptr) {
          return false;
        }
        wlr_scene_node_set_enabled(&tree->node, false);
        wlr_scene_node_place_above(&tree->node, &server.pinnedTree()->node);
        wlr_scene_node_set_position(&tree->node, box.x, box.y);
        picture = wlr_scene_buffer_create(tree, pair.display);
        if (picture == nullptr) {
          return false;
        }
        picture->point_accepts_input = [](wlr_scene_buffer*, double*, double*) { return false; };
        wlr_scene_buffer_set_dest_size(picture, box.width, box.height);
        wlr_scene_buffer_set_transform(picture, output.wlr()->transform);
        if (workingSpace) {
          wlr_scene_buffer_set_transfer_function(picture, WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR);
          wlr_scene_buffer_set_primaries(picture, WLR_COLOR_NAMED_PRIMARIES_SRGB);
        }
      } else {
        wlr_scene_buffer_set_buffer(picture, pair.display);
      }
      const bool installed = fx_scene_output_replace_range_for_test(
                                 output.sceneOutput(), &server.shellLayerTree(0)->node, &server.pinnedTree()->node
                             )
          && fx_scene_output_bind_replacement_roles_for_test(output.sceneOutput(), picture, pair.unfiltered);
      if (installed) {
        wlr_scene_node_set_enabled(&tree->node, true);
      }
      return installed;
    }
  };

  WorkspaceTransition::WorkspaceTransition(Server& server, Output& output)
      : m_state(std::make_unique<State>(server, output)) {
    server.registerAnimatable(this);
  }
  WorkspaceTransition::~WorkspaceTransition() {
    cancel(PresentationFallback::OutputRemoved);
    m_state->server.unregisterAnimatable(this);
  }
  bool WorkspaceTransition::begin(std::string_view destination, bool interactive) {
    const std::string requested(destination);
    destination = requested;
    auto& state = *m_state;
    state.awaitNative = state.awaitNative || state.tree != nullptr;
    if (state.active) {
      cancel(PresentationFallback::None);
    }
    auto* group = state.output.workspaceGroup();
    if (group == nullptr
        || group->active() == nullptr
        || group->active()->id() == destination
        || group->slideActive()
        || state.server.sessionLocked()
        || (state.server.overview() && state.server.overview()->active())) {
      return false;
    }
    auto bundle = state.server.effects().sceneAnimationEffect(AnimationEvent::Workspaces);
    if (!bundle) {
      return false;
    }
    Workspace* target = nullptr;
    for (size_t i = 0; i < group->workspaceCount(); ++i) {
      if (group->workspaceAt(i)->id() == destination)
        target = group->workspaceAt(i);
    }
    if (!target)
      return false;
    state.bundle = std::move(bundle);
    state.box = state.output.layoutBox();
    state.transform = state.output.wlr()->transform;
    state.scale = state.output.wlr()->scale;
    state.width = state.output.wlr()->width;
    state.height = state.output.wlr()->height;
    state.original = group->active()->id();
    state.destination = destination;
    state.direction = target->index() > group->active()->index() ? 1 : -1;
    state.horizontal = group->workspaceAxis() == WorkspaceAxis::Horizontal;
    state.interactive = interactive;
    state.released = !interactive;
    state.commit = true;
    state.activated = false;
    state.pendingEnd = false;
    state.lastProgress = -1;
    state.lastSources = 0;
    state.sources = std::make_unique<WorkspaceSources>(state.server, state.output, [this](PresentationFallback reason) {
      cancel(reason);
    });
    state.workingSpace = fx_scene_source_working_space(state.output.sceneOutput());
    state.floatingPoint = fx_scene_source_floating_point(state.output.sceneOutput());
    state.composition = SceneComposition::create(
        state.server.renderer(), state.server.allocator(), state.sources->resourcePool(), presentationAggregatePool(),
        state.bundle, state.width, state.height, state.workingSpace, state.floatingPoint
    );
    if (!state.composition) {
      cancel(PresentationFallback::ResourceBudget);
      return false;
    }
    if (!state.sources->beginPair(state.original, destination) || !state.sources->freezeOutgoing()) {
      cancel(
          state.sources->lastFallback() == PresentationFallback::ResourceBudget
              ? PresentationFallback::ResourceBudget
              : PresentationFallback::SourceUnavailable
      );
      return false;
    }
    state.progress.snap(0);
    state.progress.retarget(1, config().animation.workspaces.durationMs, config().animation.workspaces.curve);
    state.identity = state.progress.transitionId();
    state.seed = state.progress.shaderSeed();
    state.progress.snap(0);
    state.fallback = PresentationFallback::None;
    state.active = true;
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }
  bool WorkspaceTransition::gestureProgress(double progress) {
    auto& state = *m_state;
    if (!state.active || !state.interactive || state.released || !std::isfinite(progress))
      return false;
    state.progress.snap(std::clamp(progress, 0.0, 1.0));
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }
  bool WorkspaceTransition::retargetInteractive(std::string_view destination, double progress) {
    if (!std::isfinite(progress))
      return false;
    return begin(destination, true) && gestureProgress(progress);
  }
  bool WorkspaceTransition::settle(bool commit, double velocity) {
    auto& state = *m_state;
    if (!state.active || !state.interactive || state.released || !std::isfinite(velocity))
      return false;
    state.commit = commit;
    state.released = true;
    const double target = commit ? 1 : 0;
    const auto& settings = config().animation.workspaces;
    if (settings.curve.easing == Easing::Spring)
      state.progress.settleSpring(target, settings.curve.spring, velocity);
    else
      state.progress.retarget(target, settings.durationMs, settings.curve);
    state.progress.tick(state.server.animationClockMsec());
    if (state.sourceReady && commit) {
      state.activated = state.sources->activateSelection(state.destination);
      state.server.refocus(&state.output);
    }
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }
  void WorkspaceTransition::prepareFrame(bool animate) {
    auto& state = *m_state;
    if (!state.active)
      return;
    if (state.awaitNative)
      return;
    const auto box = state.output.layoutBox();
    if (state.server.sessionLocked()) {
      cancel(PresentationFallback::Locked);
      return;
    }
    if (box.x != state.box.x
        || box.y != state.box.y
        || box.width != state.box.width
        || box.height != state.box.height
        || state.output.wlr()->transform != state.transform
        || state.output.wlr()->scale != state.scale
        || state.output.wlr()->width != state.width
        || state.output.wlr()->height != state.height
        || fx_scene_source_working_space(state.output.sceneOutput()) != state.workingSpace
        || fx_scene_source_floating_point(state.output.sceneOutput()) != state.floatingPoint) {
      cancel(PresentationFallback::TopologyChanged);
      return;
    }
    const auto binding = config().animation.eventEffect(AnimationEvent::Workspaces);
    if (!config().animation.enabled
        || !binding.enabled
        || !binding.effect
        || *binding.effect != state.bundle->definition.name) {
      cancel(PresentationFallback::BindingRemoved);
      return;
    }
    if (state.composition->pending())
      return;
    const auto ready = state.sources->prepareFrame(animate);
    if (ready == WorkspaceSourceResult::Preparing)
      return;
    if (ready != WorkspaceSourceResult::Ready) {
      cancel(
          state.sources->lastFallback() == PresentationFallback::ResourceBudget
              ? PresentationFallback::ResourceBudget
              : PresentationFallback::SourceUnavailable
      );
      return;
    }
    auto faces = state.sources->faces();
    if (faces.size() != 2) {
      cancel(PresentationFallback::SourceUnavailable);
      return;
    }
    if (!state.sourceReady) {
      state.sourceReady = true;
      if (!state.interactive) {
        state.progress.retarget(1, config().animation.workspaces.durationMs, config().animation.workspaces.curve);
        state.progress.tick(state.server.animationClockMsec());
      }
      if (state.released && state.commit) {
        state.activated = state.sources->activateSelection(state.destination);
        if (!state.activated) {
          cancel(PresentationFallback::SourceUnavailable);
          return;
        }
        state.server.refocus(&state.output);
      }
      state.server.effects().updateSceneTime(this, &state.output, state.bundle.get(), true);
    }
    fx_scene_frame frame{};
    state.server.effects().fillScenePalette(frame, *state.bundle);
    const auto revision = state.sources->revision();
    if (revision == state.lastSources
        && state.lastProgress == state.progress.current()
        && (!state.bundle->readsTime || state.lastTime == state.output.effectSeconds())
        && frame.palette_count == state.lastPaletteCount
        && std::ranges::equal(frame.palette, state.lastPalette))
      return;
    frame.output_size[0] = static_cast<float>(state.box.width);
    frame.output_size[1] = static_cast<float>(state.box.height);
    frame.scale = state.scale;
    frame.output_transform = state.transform;
    frame.time = state.output.effectSeconds();
    frame.progress = static_cast<float>(state.progress.current());
    frame.linear_progress = state.interactive ? frame.progress : static_cast<float>(state.progress.progress());
    frame.direction = state.direction;
    frame.axis[state.horizontal ? 0 : 1] = 1;
    frame.scene_count = 2;
    frame.viewport[2] = frame.output_size[0];
    frame.viewport[3] = frame.output_size[1];
    std::ranges::copy(state.seed, frame.random_seed);
    const auto matrix = sourceMatrix(state.transform);
    const std::array<SceneComposition::Source, 2> inputs{
        {{faces[0].display, faces[0].unfiltered, matrix.data()}, {faces[1].display, faces[1].unfiltered, matrix.data()}}
    };
    if (!state.composition->render(frame, inputs) || !state.install(state.composition->candidate())) {
      cancel(PresentationFallback::CompositionFailure);
      return;
    }
    state.pendingEnd = state.released && !state.progress.animating();
    state.lastSources = revision;
    state.lastProgress = state.progress.current();
    state.lastTime = frame.time;
    state.lastPaletteCount = frame.palette_count;
    std::ranges::copy(frame.palette, state.lastPalette.begin());
  }
  void WorkspaceTransition::frameSubmitted(bool success) {
    auto& state = *m_state;
    if (success && state.awaitNative) {
      state.awaitNative = false;
      if (state.active)
        wlr_output_schedule_frame(state.output.wlr());
      return;
    }
    if (!state.active || !state.composition->pending())
      return;
    state.composition->submitted(success);
    state.sources->frameSubmitted(success);
    if (success && state.pendingEnd)
      cancel(PresentationFallback::None);
  }
  void WorkspaceTransition::sendFrameDone(const timespec& when) {
    if (m_state->active)
      m_state->sources->sendFrameDone(when);
  }
  void WorkspaceTransition::cancel(PresentationFallback reason) {
    auto& state = *m_state;
    const bool visible = state.tree != nullptr;
    state.awaitNative = state.awaitNative || visible;
    const bool finishRequestedSwitch = state.active
        && !state.activated
        && state.released
        && state.commit
        && reason != PresentationFallback::TopologyChanged
        && reason != PresentationFallback::OutputRemoved;
    // A requested timed switch remains authoritative even if source preparation
    // fails. Inventory invalidation has already selected its own new target.
    if (state.active
        && state.sources
        && !state.activated
        && state.released
        && state.commit
        && reason != PresentationFallback::OutputRemoved) {
      state.activated = state.sources->activateSelection(state.destination);
    }
    state.active = false;
    state.fallback = reason;
    state.sourceReady = false;
    state.server.effects().clearSceneTime(this);
    if (state.tree) {
      wlr_scene_node_set_enabled(&state.tree->node, false);
      fx_scene_output_replace_range_for_test(state.output.sceneOutput(), nullptr, nullptr);
      wlr_scene_node_destroy(&state.tree->node);
      state.tree = nullptr;
      state.picture = nullptr;
    }
    state.composition.reset();
    if (state.sources)
      state.sources->cancel(reason);
    if (finishRequestedSwitch && !state.activated) {
      auto* group = state.output.workspaceGroup();
      if (group != nullptr) {
        for (size_t i = 0; i < group->workspaceCount(); ++i) {
          auto* target = group->workspaceAt(i);
          if (target->id() == state.destination) {
            group->activate(target, false);
            state.server.refocus(&state.output);
            break;
          }
        }
      }
    }
    state.progress.snap(state.commit ? 1 : 0);

    if (visible)
      wlr_output_schedule_frame(state.output.wlr());
  }
  bool WorkspaceTransition::active() const { return m_state->active; }
  bool WorkspaceTransition::interactive() const { return m_state->interactive && !m_state->released; }
  bool WorkspaceTransition::tickAnimations(uint64_t nowMsec) {
    if (!m_state->active || !m_state->sourceReady)
      return false;
    m_state->progress.tick(nowMsec);
    return hasActiveAnimations();
  }
  bool WorkspaceTransition::hasActiveAnimations() const { return m_state->active && m_state->progress.animating(); }
  bool WorkspaceTransition::animatesOn(const Output* output) const { return &m_state->output == output; }
  nlohmann::json WorkspaceTransition::status() const {
    const auto& state = *m_state;
    constexpr std::array reasons{
        "",
        "unsupported_capability",
        "source_unavailable",
        "resource_budget",
        "composition_failure",
        "topology_changed",
        "renderer_lost",
        "output_removed",
        "locked",
        "binding_removed",
    };
    return {
        {"active", state.active},
        {"preset", state.bundle ? state.bundle->definition.name : ""},
        {"identity", state.identity},
        {"from", state.original},
        {"to", state.destination},
        {"progress", state.progress.current()},
        {"interactive", interactive()},
        {"source_ready", state.sourceReady},
        {"memory_bytes", state.sources ? state.sources->reservedBytes() : 0},
        {"fallback", reasons[static_cast<size_t>(state.fallback)]},
        {"sources", state.sources ? state.sources->status() : nlohmann::json()}
    };
  }
} // namespace umbriel
