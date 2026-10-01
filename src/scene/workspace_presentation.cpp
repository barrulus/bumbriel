#include "scene/workspace_presentation.h"

#include "config/config.h"
#include "input/cursor.h"
#include "output/output.h"
#include "overview/navigation.h"
#include "overview/overview.h"
#include "scene/composition.h"
#include "scene/effect_registry.h"
#include "scene/window_presentation.h"
#include "scene/workspace_sources.h"
#include "scene/workspace_transition.h"
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
    for (const auto& output : outputs()) {
      if (auto* mode = output->workspacePresentation()) {
        mode->cancel(reason);
      }
      if (auto* mode = output->workspaceTransition()) {
        mode->cancel(reason);
      }
      if (auto* mode = output->windowPresentation()) {
        mode->cancel(reason);
      }
    }
  }
  WindowPresentation& Output::ensureWindowPresentation() {
    if (!m_windowPresentation) {
      m_windowPresentation = std::make_unique<WindowPresentation>(*m_server, *this);
    }
    return *m_windowPresentation;
  }
  bool Output::enterWorkspacePresentation(std::string_view preset) {
    if (!m_workspacePresentation) {
      m_workspacePresentation = std::make_unique<WorkspacePresentation>(*m_server, *this);
    }
    return m_workspacePresentation->begin(preset);
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
    struct LiveFace final : PresentationVisualSource {
      std::shared_ptr<WorkspaceSources> owner;
      std::string identity;
      LiveFace(std::shared_ptr<WorkspaceSources> provider, std::string id)
          : owner(std::move(provider)), identity(std::move(id)) {}
    };
  } // namespace

  struct WorkspacePresentation::State {
    Server& server;
    Output& output;
    std::shared_ptr<const scene_experiment::ProgramBundle> bundle;
    std::shared_ptr<WorkspaceSources> sources;
    std::unique_ptr<SceneComposition> composition;
    PresentationLease lease;
    PresentationInventory inventory;
    AnimatedValue progress;
    std::array<float, 4> seed{};
    AnimatedValue navigation;
    OverviewNavigation fingerNavigation;
    NavigationSource navigationSource = NavigationSource::Swipe;
    wlr_pointer* navigationPointer = nullptr;
    wl_listener navigationDeviceDestroy{};
    WorkspacePresentation* navigationOwner = nullptr;
    double navigationStart = 0;
    double fingerVelocity = 0;
    int navigationOriginal = 0;
    double scrollDx = 0;
    double scrollDy = 0;
    uint32_t scrollTime = 0;
    bool scrollStopX = false;
    bool scrollStopY = false;
    fx_scene_mesh mesh{};
    wlr_scene_tree* tree = nullptr;
    wlr_scene_buffer* picture = nullptr;
    wlr_box box{};
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    float scale = 1;
    int width = 0;
    int height = 0;
    std::string original;
    std::string clickedWindow;
    bool active = false;
    bool inputOwned = false;
    bool exiting = false;
    bool commitSelection = false;
    bool pendingExit = false;
    bool workingSpace = false;
    bool floatingPoint = false;
    bool fitAll = false;
    uint64_t lastSources = 0;
    uint64_t lastAudio = 0;
    double lastProgress = -1;
    double lastNavigation = -1;
    float lastNavigationVelocity = 0;
    std::array<float, 16> lastPalette{};
    int lastPaletteCount = 0;
    float lastTime = 0;
    PresentationFallback fallback = PresentationFallback::None;
    State(Server& server, Output& output) : server(server), output(output) {}
    ~State() { fx_scene_mesh_finish(&mesh); }

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

  WorkspacePresentation::WorkspacePresentation(Server& server, Output& output)
      : m_state(std::make_unique<State>(server, output)) {
    server.registerAnimatable(this);
  }
  WorkspacePresentation::~WorkspacePresentation() {
    cancel(PresentationFallback::OutputRemoved);
    m_state->server.unregisterAnimatable(this);
  }

  bool WorkspacePresentation::begin(std::string_view preset) {
    auto& state = *m_state;
    if (state.active
        || (state.output.workspaceTransition() && state.output.workspaceTransition()->active())
        || (state.output.windowPresentation() && state.output.windowPresentation()->active())
        || state.server.sessionLocked()
        || (state.server.overview() != nullptr && state.server.overview()->active())) {
      return false;
    }
    auto* group = state.output.workspaceGroup();
    if (group == nullptr || group->active() == nullptr || group->slideActive()) {
      return false;
    }
    auto bundle = state.server.effects().scenePreset(preset, scene_experiment::Scope::WorkspaceSet);
    if (!bundle) {
      state.fallback = PresentationFallback::UnsupportedCapability;
      return false;
    }
    state.bundle = std::move(bundle);
    state.fitAll = config().workspacePresentation.framing == Config::WorkspacePresentation::Framing::FitAll;
    state.box = state.output.layoutBox();
    state.transform = state.output.wlr()->transform;
    state.scale = state.output.wlr()->scale;
    state.width = state.output.wlr()->width;
    state.height = state.output.wlr()->height;
    state.original = group->active()->id();
    state.clickedWindow.clear();
    state.sources = std::make_shared<WorkspaceSources>(state.server, state.output, [this](PresentationFallback reason) {
      cancel(reason);
    });
    state.workingSpace = fx_scene_source_working_space(state.output.sceneOutput());
    state.floatingPoint = fx_scene_source_floating_point(state.output.sceneOutput());
    state.composition = SceneComposition::create(
        state.server.renderer(), state.server.allocator(), state.sources->resourcePool(), presentationAggregatePool(),
        state.bundle, state.output.wlr()->width, state.output.wlr()->height, state.workingSpace, state.floatingPoint
    );
    if (!state.composition || !state.sources->begin(state.original)) {
      cancel(PresentationFallback::ResourceBudget);
      return false;
    }
    std::vector<std::string> identities;
    identities.reserve(group->workspaceCount());
    for (size_t i = 0; i < group->workspaceCount(); ++i) {
      identities.push_back(group->workspaceAt(i)->id());
    }
    if (!state.inventory.enter(identities, state.original)
        || !state.sources->setVisibleWorkspaces(identities)
        || !state.sources->requestLanding(state.original)
        || !fx_scene_mesh_create(&state.mesh, 1, 1, static_cast<unsigned>(identities.size()))) {
      cancel(PresentationFallback::SourceUnavailable);
      return false;
    }
    state.progress.snap(0);
    state.navigation.snap(static_cast<double>(std::ranges::find(identities, state.original) - identities.begin()));
    state.exiting = state.commitSelection = state.pendingExit = false;
    state.lastProgress = state.lastNavigation = -1;
    state.lastSources = state.lastAudio = 0;
    state.fallback = PresentationFallback::None;
    state.active = true;
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }

  bool WorkspacePresentation::select(std::string_view identity) {
    auto& state = *m_state;
    if (!state.active || !state.lease.active() || state.exiting || !state.inventory.select(std::string(identity))) {
      return false;
    }
    if (state.navigationPointer != nullptr) {
      wl_list_remove(&state.navigationDeviceDestroy.link);
      state.navigationPointer = nullptr;
    }
    const auto& identities = state.inventory.workspaces();
    const auto index = std::ranges::find(identities, identity) - identities.begin();
    auto target = static_cast<double>(index);
    if (identities.size() > 1) {
      const auto count = static_cast<double>(identities.size());
      target += std::round((state.navigation.current() - target) / count) * count;
    }
    state.navigation.settleSpring(
        target, config().animation.overview.workspaceCurve.spring, state.navigation.velocity()
    );
    state.lease.setPhase(PresentationPhase::Settling);
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }
  bool WorkspacePresentation::step(int direction) {
    auto& state = *m_state;
    if (!state.active || state.exiting) {
      return false;
    }
    auto selection = state.inventory;
    const double target = state.navigation.target() + direction;
    const double velocity = state.navigation.velocity();
    if (!selection.step(direction, true) || !select(*selection.selected())) {
      return false;
    }
    if (state.inventory.workspaces().size() > 1) {
      // Keep one-step motion through the seam instead of traversing the other
      // N-1 faces back to the selected identity's canonical ordinal.
      state.navigation.settleSpring(target, config().animation.overview.workspaceCurve.spring, velocity);
    }
    return true;
  }
  bool WorkspacePresentation::beginNavigation(wlr_pointer* pointer, NavigationSource source) {
    auto& state = *m_state;
    endNavigation(true, 0);
    if (!state.active || !state.lease.active() || state.exiting || pointer == nullptr) {
      return false;
    }
    state.navigationOwner = this;
    state.navigationPointer = pointer;
    state.navigationSource = source;
    state.navigationStart = state.navigation.current();
    state.fingerVelocity = 0;
    const auto& identities = state.inventory.workspaces();
    state.navigationOriginal =
        static_cast<int>(std::ranges::find(identities, *state.inventory.selected()) - identities.begin());
    state.navigation.snap(state.navigationStart);
    state.fingerNavigation.reset();
    state.scrollDx = state.scrollDy = 0;
    state.scrollStopX = state.scrollStopY = false;
    state.navigationDeviceDestroy.notify = [](wl_listener* listener, void*) {
      State* self;
      self = wl_container_of(listener, self, navigationDeviceDestroy);
      self->navigationOwner->endNavigation(true, 0);
    };
    wl_signal_add(&pointer->base.events.destroy, &state.navigationDeviceDestroy);
    return true;
  }

  void WorkspacePresentation::updateNavigation(double dx, double dy, uint32_t timeMsec) {
    auto& state = *m_state;
    if (!state.active || state.exiting || state.navigationPointer == nullptr) {
      return;
    }
    state.fingerNavigation.update(dx, dy, timeMsec);
    if (state.fingerNavigation.axis() == OverviewNavigation::Axis::Pending) {
      return;
    }
    const bool horizontal = state.fingerNavigation.axis() == OverviewNavigation::Axis::Horizontal;
    const double factor =
        horizontal ? config().overview.scrollFactorHorizontal : config().overview.scrollFactorVertical;
    const double scale = factor / OverviewNavigation::travelFor(state.navigationSource).workspace;
    const auto last = static_cast<double>(state.inventory.workspaces().size() - 1);
    const double position = state.navigationStart + state.fingerNavigation.position() * scale;
    state.fingerVelocity = state.fingerNavigation.velocity()
        * scale
        * (last > 0 ? 1 : GesturePhysics::rubberBandDerivative(position, 0, last, GesturePhysics::kOverscrollLimit));
    state.navigation.snap(
        last > 0 ? position : GesturePhysics::rubberBand(position, 0, last, GesturePhysics::kOverscrollLimit)
    );
    state.lease.setPhase(PresentationPhase::Settling);
    wlr_output_schedule_frame(state.output.wlr());
  }

  void WorkspacePresentation::endNavigation(bool cancelled, uint32_t timeMsec) {
    auto& state = *m_state;
    if (state.navigationPointer == nullptr) {
      return;
    }
    wl_list_remove(&state.navigationDeviceDestroy.link);
    state.navigationPointer = nullptr;
    state.scrollDx = state.scrollDy = 0;
    state.scrollStopX = state.scrollStopY = false;
    if (!state.active || state.exiting || state.fingerNavigation.axis() == OverviewNavigation::Axis::Pending) {
      return;
    }
    state.fingerNavigation.update(0, 0, timeMsec);
    const bool horizontal = state.fingerNavigation.axis() == OverviewNavigation::Axis::Horizontal;
    const double factor =
        horizontal ? config().overview.scrollFactorHorizontal : config().overview.scrollFactorVertical;
    const double scale = factor / OverviewNavigation::travelFor(state.navigationSource).workspace;
    const auto& identities = state.inventory.workspaces();
    const int last = static_cast<int>(identities.size() - 1);
    const bool cyclic = last > 0;
    const double projected = state.navigationStart + state.fingerNavigation.projectedPosition() * scale;
    const auto count = static_cast<double>(identities.size());
    const double target = cancelled ? state.navigationOriginal
            + (cyclic ? std::round((state.navigationStart - state.navigationOriginal) / count) * count : 0)
        : cyclic ? std::round(projected)
                 : GesturePhysics::stepTarget(projected, 0, last);
    const auto selected = static_cast<size_t>(std::fmod(std::fmod(target, count) + count, count));
    const double velocity = cancelled ? 0
                                      : state.fingerNavigation.velocity()
            * scale
            * (cyclic ? 1
                      : GesturePhysics::rubberBandDerivative(
                            state.navigation.current(), 0, last, GesturePhysics::kOverscrollLimit
                        ));
    if (select(identities[selected])) {
      state.navigation.settleSpring(target, config().animation.overview.workspaceCurve.spring, velocity);
    }
  }

  void WorkspacePresentation::handleTouchpadAxis(wlr_pointer* pointer, bool vertical, double delta, uint32_t timeMsec) {
    auto& state = *m_state;
    if (state.navigationPointer != pointer || state.navigationSource != NavigationSource::Scroll) {
      if (delta == 0 || !beginNavigation(pointer, NavigationSource::Scroll)) {
        return;
      }
    }
    if (vertical) {
      state.scrollDy += delta;
      state.scrollStopY = delta == 0;
    } else {
      state.scrollDx += delta;
      state.scrollStopX = delta == 0;
    }
    state.scrollTime = timeMsec;
  }

  void WorkspacePresentation::handleTouchpadFrame() {
    auto& state = *m_state;
    if (state.navigationPointer == nullptr || state.navigationSource != NavigationSource::Scroll) {
      return;
    }
    if (state.scrollDx != 0 || state.scrollDy != 0) {
      updateNavigation(state.scrollDx, state.scrollDy, state.scrollTime);
    }
    const auto axis = state.fingerNavigation.axis();
    const bool stop = axis == OverviewNavigation::Axis::Horizontal ? state.scrollStopX
        : axis == OverviewNavigation::Axis::Vertical               ? state.scrollStopY
                                                                   : state.scrollStopX || state.scrollStopY;
    state.scrollDx = state.scrollDy = 0;
    state.scrollStopX = state.scrollStopY = false;
    if (stop) {
      endNavigation(false, state.scrollTime);
    }
  }

  bool WorkspacePresentation::activateAt(double layoutX, double layoutY) {
    auto& state = *m_state;
    if (!state.active) {
      return false;
    }
    // The entire first input sequence belongs to the presentation, including
    // misses and shaders without an unambiguous sampled-window pick contract.
    if (!state.lease.active() || state.exiting || !state.composition) {
      return true;
    }
    const auto projected = state.composition->pick(layoutX - state.box.x, layoutY - state.box.y);
    if (!projected) {
      return true;
    }
    const auto source = state.sources->pick(projected->ordinal, projected->u, projected->v);
    if (source && select(source->identity) && accept()) {
      state.clickedWindow = source->window;
    }
    return true;
  }

  bool WorkspacePresentation::accept() {
    auto& state = *m_state;
    if (!state.active || !state.lease.active() || state.exiting) {
      return false;
    }
    if (state.navigationPointer != nullptr) {
      wl_list_remove(&state.navigationDeviceDestroy.link);
      state.navigationPointer = nullptr;
    }
    state.commitSelection = true;
    state.exiting = true;
    state.lease.setPhase(PresentationPhase::Exiting);
    state.progress.retarget(0, config().animation.workspaces.durationMs, config().animation.workspaces.curve);
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }
  bool WorkspacePresentation::dismiss() {
    if (!m_state->active || m_state->exiting || !select(m_state->original)) {
      return false;
    }
    if (!accept()) {
      return false;
    }
    m_state->commitSelection = false;
    return true;
  }

  void WorkspacePresentation::prepareFrame(bool animate) {
    auto& state = *m_state;
    if (!state.active) {
      return;
    }
    if (state.server.sessionLocked()) {
      cancel(PresentationFallback::Locked);
      return;
    }
    const auto box = state.output.layoutBox();
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
    if (config().workspacePresentation.effect != state.bundle->definition.name
        || state.fitAll != (config().workspacePresentation.framing == Config::WorkspacePresentation::Framing::FitAll)) {
      cancel(PresentationFallback::BindingRemoved);
      return;
    }
    if (state.composition->pending()) {
      return; // Provider and composed roles retain the same failed candidate.
    }
    if (state.exiting && !state.sources->requestLanding(*state.inventory.selected())) {
      cancel(PresentationFallback::SourceUnavailable);
      return;
    }
    const float progress = static_cast<float>(std::clamp(state.progress.current(), 0.0, 1.0));
    // Present one moving face throughout the transition. The native-quality
    // capture takes over only at the matching viewport endpoint; blending it
    // with fit_all framing produces two differently sized copies of each window.
    if (!state.sources->setFramingProgress(state.fitAll ? progress : 0)
        || !state.sources->setLandingMix(progress == 0 ? 1 : 0)) {
      cancel(PresentationFallback::SourceUnavailable);
      return;
    }
    const auto ready = state.sources->prepareFrame(animate);
    if (ready == WorkspaceSourceResult::Preparing) {
      return;
    }
    if (ready != WorkspaceSourceResult::Ready) {
      cancel(
          state.sources->lastFallback() == PresentationFallback::ResourceBudget
              ? PresentationFallback::ResourceBudget
              : PresentationFallback::SourceUnavailable
      );
      return;
    }
    const auto faces = state.sources->faces();
    if (!state.lease.active()) {
      PresentationRequest request;
      request.scope = PresentationScope::WorkspaceSet;
      request.identity = nextAnimationTransitionId();
      request.workspaces = state.inventory.workspaces();
      request.destination = state.original;
      for (const auto& face : faces) {
        auto source = std::make_shared<LiveFace>(state.sources, face.identity);
        request.sources.push_back({source, source});
      }
      state.inputOwned = state.output.beginSceneInput([this] { cancel(PresentationFallback::InputDismissal); });
      if (!state.inputOwned || !state.lease.acquire(std::move(request))) {
        cancel(PresentationFallback::InputGrab);
        return;
      }
      state.progress.retarget(1, config().animation.workspaces.durationMs, config().animation.workspaces.curve);
      state.seed = state.progress.shaderSeed();
      state.progress.tick(state.server.animationClockMsec());
      state.server.effects().updateSceneAudio(this, &state.output, state.bundle.get(), true);
    }
    const auto audio = state.server.effects().audioInputRevision(&state.output);
    const auto revision = state.sources->revision();
    fx_scene_frame frame{};
    state.server.effects().fillScenePalette(frame, *state.bundle);
    frame.navigation_velocity =
        static_cast<float>(state.navigationPointer != nullptr ? state.fingerVelocity : state.navigation.velocity());
    if (revision == state.lastSources
        && audio == state.lastAudio
        && state.progress.current() == state.lastProgress
        && state.navigation.current() == state.lastNavigation
        && frame.navigation_velocity == state.lastNavigationVelocity
        && frame.palette_count == state.lastPaletteCount
        && std::ranges::equal(frame.palette, state.lastPalette)
        && (!state.bundle->readsTime || state.output.effectSeconds() == state.lastTime)) {
      return;
    }
    frame.output_size[0] = static_cast<float>(state.box.width);
    frame.output_size[1] = static_cast<float>(state.box.height);
    frame.scale = state.output.wlr()->scale;
    frame.output_transform = state.output.wlr()->transform;
    frame.time = state.output.effectSeconds();
    frame.progress = static_cast<float>(state.progress.current());
    frame.linear_progress = static_cast<float>(state.progress.progress());
    frame.navigation_position = static_cast<float>(state.navigation.current());
    frame.scene_count = static_cast<int>(faces.size());
    frame.framing = state.fitAll ? 1 : 0;
    frame.viewport[2] = frame.output_size[0];
    frame.viewport[3] = frame.output_size[1];
    std::ranges::copy(state.seed, frame.random_seed);
    state.server.effects().fillSceneAudio(frame, *state.bundle, &state.output);
    const auto matrix = sourceMatrix(state.output.wlr()->transform);
    std::vector<SceneComposition::Source> inputs;
    std::vector<fx_scene_draw> draws;
    for (size_t i = 0; i < faces.size(); ++i) {
      const auto& face = faces[i];
      inputs.push_back({face.display, face.unfiltered, matrix.data()});
      auto& draw = draws.emplace_back();
      draw.mesh = &state.mesh;
      draw.item.kind = FX_SCENE_FACE;
      draw.item.ordinal = static_cast<int>(i);
      draw.item.token = static_cast<int>(i + 1);
      draw.item.native_opacity = 1;
      std::ranges::copy(frame.viewport, draw.item.current_box);
      std::ranges::copy(frame.viewport, draw.item.source_box);
      std::ranges::copy(frame.viewport, draw.item.destination_box);
      const auto relativeBox = [&](const wlr_box& box, float* result) {
        result[0] = static_cast<float>(box.x - state.box.x);
        result[1] = static_cast<float>(box.y - state.box.y);
        result[2] = static_cast<float>(box.width);
        result[3] = static_cast<float>(box.height);
      };
      relativeBox(face.extent, draw.item.capture_extent);
      relativeBox(face.contentBounds, draw.item.content_bounds);
      std::ranges::copy(frame.viewport, draw.item.coverage_box);
      std::ranges::copy(face.contentFraming, draw.item.framing_transform);
      draw.item.framing_transform[2] += static_cast<float>(state.box.x) * draw.item.framing_transform[0];
      draw.item.framing_transform[3] += static_cast<float>(state.box.y) * draw.item.framing_transform[1];
    }
    if (!state.composition->render(frame, inputs, draws) || !state.install(state.composition->candidate())) {
      cancel(PresentationFallback::CompositionFailure);
      return;
    }
    state.pendingExit = state.exiting && !state.progress.animating() && !state.navigation.animating();
    state.lastSources = revision;
    state.lastAudio = audio;
    state.lastProgress = state.progress.current();
    state.lastNavigation = state.navigation.current();
    state.lastNavigationVelocity = frame.navigation_velocity;
    std::ranges::copy(frame.palette, state.lastPalette.begin());
    state.lastPaletteCount = frame.palette_count;
    state.lastTime = frame.time;
  }

  void WorkspacePresentation::frameSubmitted(bool success) {
    auto& state = *m_state;
    if (!state.active || !state.composition->pending()) {
      return;
    }
    state.composition->submitted(success);
    state.sources->frameSubmitted(success);
    if (success && state.pendingExit) {
      const std::string destination = state.commitSelection ? *state.inventory.selected() : state.original;
      const bool committed = state.sources->commitSelection(destination);
      if (committed && state.commitSelection && !state.clickedWindow.empty()) {
        if (auto* cursor = state.server.cursor()) {
          cursor->setSceneRestoreFocus(state.output, state.clickedWindow, destination);
        }
      }
      cancel(committed ? PresentationFallback::None : PresentationFallback::SourceUnavailable);
    }
  }
  void WorkspacePresentation::sendFrameDone(const timespec& when) {
    if (m_state->active) {
      m_state->sources->sendFrameDone(when);
    }
  }
  void WorkspacePresentation::cancel(PresentationFallback reason) {
    auto& state = *m_state;
    if (state.navigationPointer != nullptr) {
      wl_list_remove(&state.navigationDeviceDestroy.link);
      state.navigationPointer = nullptr;
    }
    const bool visible = state.tree != nullptr;
    if (reason != PresentationFallback::None) {
      if (auto* cursor = state.server.cursor()) {
        cursor->setSceneRestoreFocus(state.output, {}, {});
      }
    }
    state.clickedWindow.clear();
    state.active = false;
    state.fallback = reason;
    state.lease.cancel(reason);
    state.server.effects().clearSceneAudio(this);
    if (state.tree != nullptr) {
      wlr_scene_node_set_enabled(&state.tree->node, false);
      fx_scene_output_replace_range_for_test(state.output.sceneOutput(), nullptr, nullptr);
      wlr_scene_node_destroy(&state.tree->node);
      state.tree = nullptr;
      state.picture = nullptr;
    }
    state.composition.reset();
    if (state.sources) {
      state.sources->cancel(reason);
    }
    fx_scene_mesh_finish(&state.mesh);
    state.progress.snap(0);
    state.navigation.snap(0);
    if (state.inputOwned) {
      state.output.endSceneInput(visible);
      state.inputOwned = false;
    }
    if (visible) {
      wlr_output_schedule_frame(state.output.wlr());
    }
  }
  bool WorkspacePresentation::active() const { return m_state->active; }
  bool WorkspacePresentation::tickAnimations(uint64_t nowMsec) {
    auto& state = *m_state;
    if (!state.active || !state.lease.active()) {
      return false;
    }
    state.progress.tick(nowMsec);
    state.navigation.tick(nowMsec);
    if (!state.exiting && !state.progress.animating() && !state.navigation.animating()) {
      state.lease.setPhase(PresentationPhase::Held);
    }
    return hasActiveAnimations();
  }
  bool WorkspacePresentation::hasActiveAnimations() const {
    return m_state->active && (m_state->progress.animating() || m_state->navigation.animating());
  }
  bool WorkspacePresentation::animatesOn(const Output* output) const { return &m_state->output == output; }
  nlohmann::json WorkspacePresentation::status() const {
    const auto& state = *m_state;
    const auto phase = [&] {
      switch (state.lease.phase()) {
      case PresentationPhase::Timed:
        return "timed";
      case PresentationPhase::Entering:
        return "entering";
      case PresentationPhase::Held:
        return "held";
      case PresentationPhase::Settling:
        return "settling";
      case PresentationPhase::Exiting:
        return "exiting";
      }
      return "unknown";
    };
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
    return {
        {"active", state.active},
        {"preset", state.bundle ? state.bundle->definition.name : ""},
        {"phase", phase()},
        {"progress", state.progress.current()},
        {"navigation", state.navigation.current()},
        {"click_target", state.clickedWindow},
        {"navigation_velocity",
         state.navigationPointer != nullptr ? state.fingerVelocity : state.navigation.velocity()},
        {"sources", state.sources ? state.sources->status() : nlohmann::json()},
        {"memory_bytes", state.sources ? state.sources->reservedBytes() : 0},
        {"fallback", fallback()}
    };
  }
} // namespace umbriel
