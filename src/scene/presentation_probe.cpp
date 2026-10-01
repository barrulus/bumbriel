#include "scene/presentation_probe.h"

#ifdef UMBRIEL_TEST_IPC
#include "input/cursor.h"
#include "output/output.h"
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"
#include "workspace/workspace.h"

extern "C" {
#include "../../umbrielfx/internal/render/fx_renderer/scene_resources.h"
#include "../../umbrielfx/internal/types/wlr_scene.h"

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/interfaces/wlr_touch.h>
}

#include <algorithm>
#include <cmath>
#include <drm_fourcc.h>
#include <nlohmann/json.hpp>
#include <sstream>

namespace umbriel {
  bool Server::presentationTouchProbe(std::string_view argument) {
    std::istringstream input{std::string(argument)};
    std::string action;
    input >> action;
    if (action == "create") {
      if (m_presentationTouch != nullptr) {
        return false;
      }
      static const wlr_touch_impl implementation{.name = "umbriel-c0-touch"};
      m_presentationTouch = new wlr_touch{};
      wlr_touch_init(m_presentationTouch, &implementation, "umbriel-c0-touch");
      addTouch(&m_presentationTouch->base);
      updateSeatCapabilities();
      return true;
    }
    if (action == "destroy") {
      if (m_presentationTouch != nullptr) {
        wlr_touch_finish(m_presentationTouch);
        delete m_presentationTouch;
        m_presentationTouch = nullptr;
      }
      return true;
    }
    int32_t id = 0;
    if (m_presentationTouch == nullptr || !(input >> id)) {
      return false;
    }
    const auto time = static_cast<uint32_t>(animationClockMsec());
    if (action == "up") {
      wlr_touch_up_event event{.touch = m_presentationTouch, .time_msec = time, .touch_id = id};
      wl_signal_emit_mutable(&m_presentationTouch->events.up, &event);
    } else if (action == "cancel") {
      wlr_touch_cancel_event event{.touch = m_presentationTouch, .time_msec = time, .touch_id = id};
      wl_signal_emit_mutable(&m_presentationTouch->events.cancel, &event);
    } else {
      double x = 0;
      double y = 0;
      if (!(input >> x >> y) || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) {
        return false;
      }
      if (action == "down") {
        wlr_touch_down_event event{.touch = m_presentationTouch, .time_msec = time, .touch_id = id, .x = x, .y = y};
        wl_signal_emit_mutable(&m_presentationTouch->events.down, &event);
      } else if (action == "motion") {
        wlr_touch_motion_event event{.touch = m_presentationTouch, .time_msec = time, .touch_id = id, .x = x, .y = y};
        wl_signal_emit_mutable(&m_presentationTouch->events.motion, &event);
      } else {
        return false;
      }
    }
    wl_signal_emit_mutable(&m_presentationTouch->events.frame, m_presentationTouch);
    return true;
  }

  namespace {

    struct ProbeBacking {
      wlr_buffer base{};
      uint32_t pixel = 0xff000000;
    };

    const wlr_buffer_impl backingImplementation = {
        .destroy =
            [](wlr_buffer* base) {
              ProbeBacking* backing;
              backing = wl_container_of(base, backing, base);
              delete backing;
            },
        .get_dmabuf = nullptr,
        .get_shm = nullptr,
        .begin_data_ptr_access =
            [](wlr_buffer* base, uint32_t, void** data, uint32_t* format, size_t* stride) {
              ProbeBacking* backing;
              backing = wl_container_of(base, backing, base);
              *data = &backing->pixel;
              *format = DRM_FORMAT_ARGB8888;
              *stride = sizeof(backing->pixel);
              return true;
            },
        .end_data_ptr_access = [](wlr_buffer*) {},
    };

    struct ProbeSource final : PresentationVisualSource {
      fx_scene_reservation reservation{};
      fx_scene_source_pair_for_test pair{};
      ~ProbeSource() override {
        fx_scene_source_pair_finish_for_test(&pair);
        fx_scene_release(&reservation);
      }
    };

    const char* fallbackName(PresentationFallback reason) {
      switch (reason) {
      case PresentationFallback::None:
        return "none";
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
    }
  } // namespace

  struct PresentationProbe::State {
    Server& server;
    Output& output;
    fx_scene_resource_pool outputPool{.limit = FX_SCENE_OUTPUT_BUDGET, .used = 0};
    PresentationLease lease;
    std::shared_ptr<ProbeSource> source;
    wlr_scene_tree* tree = nullptr;
    wlr_box originalBox{};
    PresentationNativeLifecycle native;
    bool overlap = false;
    bool pendingLifecycle = false;
    bool restorePending = false;
    bool pendingTopology = false;
    std::string workspaceIdentity;

    void clearVisual() {
      if (tree == nullptr) {
        source.reset();
        return;
      }
      restorePending = true;
      server.cursor()->setPresentationRestorePending(true);
      fx_scene_output_replace_range_for_test(output.sceneOutput(), nullptr, nullptr);
      wlr_scene_node_destroy(&tree->node);
      tree = nullptr;
      source.reset();
      wlr_output_schedule_frame(output.wlr());
    }
  };

  PresentationProbe::PresentationProbe(Server& server, Output& output)
      : m_state(std::make_unique<State>(server, output)) {}

  PresentationProbe::~PresentationProbe() {
    cancel(PresentationFallback::OutputRemoved);
    ordinaryCommitSucceeded(); // output teardown leaves no displayed probe owner
  }

  bool PresentationProbe::arm() {
    auto& state = *m_state;
    const auto decline = [&](PresentationFallback reason) {
      state.lease.cancel(reason);
      state.clearVisual();
      return false;
    };
    if (state.lease.active() || state.restorePending) {
      return false;
    }
    // The probe does not claim multi-output or transformed source ownership.
    if (state.server.outputs().size() != 1
        || state.output.wlr()->transform != WL_OUTPUT_TRANSFORM_NORMAL
        || state.output.wlr()->scale != 1) {
      return decline(PresentationFallback::UnsupportedCapability);
    }
    const auto lifecycles = state.server.nativePresentationLifecycles(&state.output);
    if (lifecycles.empty() && state.overlap) {
      state.lease.lifecycleSettled();
      state.overlap = false;
    }
    if (state.overlap || lifecycles.size() > 1) {
      state.overlap = true;
      return decline(PresentationFallback::OverlappingLifecycle);
    }
    if (lifecycles.size() != 1 || lifecycles[0].startMsec == 0) {
      return decline(PresentationFallback::UnsupportedCapability);
    }
    // Admission happens before captures. The real pointer guard owns event
    // pairing, and is rolled back if visual preparation fails.
    if (!state.server.cursor()->setPresentationInputProbe(true)) {
      return decline(state.server.sessionLocked() ? PresentationFallback::Locked : PresentationFallback::InputGrab);
    }
    auto source = std::make_shared<ProbeSource>();
    const auto sourceBytes = fx_scene_source_pair_bytes_for_test(state.output.sceneOutput());
    const auto bytes = sourceBytes + 4096; // includes the one-pixel backing and its allocator padding
    if (sourceBytes == 0
        || !fx_scene_reserve(&source->reservation, &state.outputPool, &presentationAggregatePool(), bytes)) {
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::ResourceBudget);
    }
    auto* root = &state.server.scene()->tree;
    wlr_scene_node* first;
    first = wl_container_of(root->children.next, first, link);
    auto* last = &state.server.pinnedTree()->node;
    if (!fx_scene_source_pair_capture_for_test(state.output.sceneOutput(), first, last, bytes, &source->pair)) {
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::SourceUnavailable);
    }
    if (source->pair.working_space) {
      // The bounded C0 probe displays an untagged SDR scene buffer. Production
      // presentation must explicitly preserve working-space metadata.
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::UnsupportedCapability);
    }
    const auto* activeWorkspace = state.output.workspaceGroup()->active();
    if (activeWorkspace == nullptr) {
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::UnsupportedCapability);
    }
    state.workspaceIdentity = activeWorkspace->id();
    state.pendingTopology = false;
    state.originalBox = state.output.layoutBox();
    state.tree = wlr_scene_tree_create(root);
    if (state.tree == nullptr) {
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::CompositionFailure);
    }
    wlr_scene_node_place_above(&state.tree->node, last);
    wlr_scene_node_set_position(&state.tree->node, state.originalBox.x, state.originalBox.y);
    // An ARGB buffer with no declared opaque region draws a fully opaque black
    // backing without changing native scene coverage/output membership. An
    // opaque scene rect would cull those native inputs before rendering.
    auto* pixels = new ProbeBacking;
    wlr_buffer_init(&pixels->base, &backingImplementation, 1, 1);
    auto* backing = wlr_scene_buffer_create(state.tree, &pixels->base);
    wlr_buffer_drop(&pixels->base);
    if (backing != nullptr) {
      wlr_scene_buffer_set_dest_size(backing, state.originalBox.width, state.originalBox.height);
    }
    auto* picture = wlr_scene_buffer_create(state.tree, source->pair.display);
    if (backing == nullptr || picture == nullptr) {
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::CompositionFailure);
    }
    // A visible displacement with known pixel correspondence. This intentionally
    // proves replacement/input lifetime, not the scene deformation shader ABI.
    wlr_scene_node_set_position(&picture->node, state.originalBox.width / 8, state.originalBox.height / 8);
    wlr_scene_buffer_set_dest_size(picture, state.originalBox.width * 3 / 4, state.originalBox.height * 3 / 4);
    state.native = lifecycles[0];
    state.pendingLifecycle = false;
    PresentationRequest request{
        .scope = PresentationScope::WindowScene,
        .identity = state.native.identity,
        .startMsec = state.native.startMsec,
        .deadlineMsec = state.native.deadlineMsec,
        .sources = {{source, source}},
        .workspaces = {},
        .destination = {}
    };
    if (!state.lease.acquire(std::move(request))) {
      state.server.cursor()->setPresentationInputProbe(false);
      state.clearVisual();
      return false;
    }
    state.source = std::move(source);
    if (!fx_scene_output_replace_range_for_test(state.output.sceneOutput(), first, last)
        || !fx_scene_output_bind_replacement_roles_for_test(
            state.output.sceneOutput(), picture, state.source->pair.unfiltered
        )) {
      state.server.cursor()->setPresentationInputProbe(false);
      return decline(PresentationFallback::CompositionFailure);
    }
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }

  void PresentationProbe::cancel(PresentationFallback reason) {
    auto& state = *m_state;
    const bool wasActive = state.lease.active();
    state.pendingLifecycle = false;
    state.pendingTopology = false;
    state.lease.cancel(reason);
    state.clearVisual();
    if (wasActive && state.server.cursor() != nullptr) {
      state.server.cursor()->setPresentationInputProbe(false);
    }
  }

  void PresentationProbe::tick(uint64_t nowMsec) {
    auto& state = *m_state;
    if (!state.lease.active() && !state.overlap) {
      return;
    }
    const auto lifecycles = state.server.nativePresentationLifecycles(&state.output);
    if (state.overlap && lifecycles.empty()) {
      state.lease.lifecycleSettled();
      state.overlap = false;
    }
    if (!state.lease.active()) {
      return;
    }
    if (state.server.sessionLocked()) {
      cancel(PresentationFallback::Locked);
      return;
    }
    const auto box = state.output.layoutBox();
    const auto* activeWorkspace = state.output.workspaceGroup()->active();
    if (state.pendingTopology
        || activeWorkspace == nullptr
        || activeWorkspace->id() != state.workspaceIdentity
        || state.server.outputs().size() != 1
        || box.x != state.originalBox.x
        || box.y != state.originalBox.y
        || box.width != state.originalBox.width
        || box.height != state.originalBox.height
        || state.output.wlr()->transform != WL_OUTPUT_TRANSFORM_NORMAL
        || state.output.wlr()->scale != 1) {
      cancel(PresentationFallback::TopologyChanged);
      return;
    }
    if (state.pendingLifecycle || std::ranges::any_of(lifecycles, [&](const auto& lifecycle) {
          return lifecycle.identity != state.native.identity;
        })) {
      state.pendingLifecycle = false;
      state.overlap = !lifecycles.empty();
      state.lease.lifecycleEvent();
      if (!state.overlap) {
        state.lease.lifecycleSettled();
      }
      state.clearVisual();
      state.server.cursor()->setPresentationInputProbe(false);
      return;
    }
    if (lifecycles.empty() && nowMsec < state.native.deadlineMsec) {
      cancel(PresentationFallback::TopologyChanged);
      return;
    }
    if (state.lease.advance(nowMsec)) {
      state.clearVisual();
      state.server.cursor()->setPresentationInputProbe(false);
    }
  }

  void PresentationProbe::nativeLifecycleStarted() {
    if (m_state->lease.active()) {
      // Native map/unmap/close callbacks must not re-enter layout or focus.
      // Remember even a burst that ends before the next composition, and stop
      // replacement at that boundary before another displaced frame is built.
      m_state->pendingLifecycle = true;
      wlr_output_schedule_frame(m_state->output.wlr());
    }
  }

  void PresentationProbe::topologyWillChange() {
    if (m_state->lease.active()) {
      m_state->pendingTopology = true;
      wlr_output_schedule_frame(m_state->output.wlr());
    }
  }

  void PresentationProbe::ordinaryCommitSucceeded() {
    if (m_state->restorePending && !m_state->lease.active()) {
      m_state->restorePending = false;
      m_state->server.cursor()->setPresentationRestorePending(false);
    }
  }

  bool PresentationProbe::active() const { return m_state->lease.active(); }

  nlohmann::json PresentationProbe::status() const {
    const auto& state = *m_state;
    nlohmann::json native = nlohmann::json::array();
    for (const auto& lifecycle : state.server.nativePresentationLifecycles(&state.output)) {
      native.push_back(
          {{"identity", lifecycle.identity},
           {"start_msec", lifecycle.startMsec},
           {"deadline_msec", lifecycle.deadlineMsec},
           {"snapshot", lifecycle.snapshot}}
      );
    }
    nlohmann::json views = nlohmann::json::array();
    for (const auto& view : state.server.views()) {
      if (!view->mapped() || !view->animatesOn(&state.output)) {
        continue;
      }
      const auto box = view->presentedBox();
      views.push_back(
          {{"id", view->extForeignIdentifier() != nullptr ? view->extForeignIdentifier() : ""},
           {"title", view->toplevel()->title != nullptr ? view->toplevel()->title : ""},
           {"floating", view->floating()},
           {"box", {box.x, box.y, box.width, box.height}}}
      );
    }
    return {
        {"active", active()},
        {"restore_pending", state.restorePending},
        {"overlap", state.overlap},
        {"fallback", fallbackName(state.lease.lastFallback())},
        {"identity", state.native.identity},
        {"start_msec", state.native.startMsec},
        {"deadline_msec", state.native.deadlineMsec},
        {"snapshot", state.native.snapshot},
        {"reserved_bytes", state.outputPool.used},
        {"aggregate_bytes", presentationAggregatePool().used},
        {"native", std::move(native)},
        {"views", std::move(views)}
    };
  }

  void Server::cancelPresentationProbes(PresentationFallback reason) {
    for (const auto& output : m_outputs) {
      if (output->presentationProbeActive() || output->workspaceSourceActive()) {
        output->cancelPresentationProbe(reason);
      }
    }
  }

  nlohmann::json Output::workspaceInventoryProbe(std::string_view action) {
    if (action == "hold") {
      if (m_inventoryProbe != nullptr) {
        return {{"err", "workspace inventory already held"}};
      }
      m_inventoryProbe = m_workspaceGroup->holdPresentationInventory([this] {
        ++m_inventoryInvalidations;
        m_inventoryProbe.reset();
      });
      if (m_inventoryProbe == nullptr) {
        return {{"err", "workspace inventory admission rejected"}};
      }
    } else if (action == "release") {
      m_inventoryProbe.reset();
    } else if (action != "status") {
      return {{"err", "expected hold, release or status"}};
    }
    nlohmann::json identities = nlohmann::json::array();
    std::string original;
    if (m_inventoryProbe != nullptr) {
      identities = m_inventoryProbe->identities();
      original = m_inventoryProbe->original();
    }
    return {
        {"ok",
         {{"held", m_inventoryProbe != nullptr},
          {"ids", std::move(identities)},
          {"original", original},
          {"invalidations", m_inventoryInvalidations},
          {"reconciliation_pending", m_workspaceGroup->inventoryReconciliationPendingForTest()}}}
    };
  }

  bool Output::armPresentationProbe() {
    if (workspaceSourceActive()) {
      return false;
    }
    flushDirty();
    m_server->tickAnimations(m_server->animationClockMsec());
    if (!m_presentationProbe) {
      m_presentationProbe = std::make_unique<PresentationProbe>(*m_server, *this);
    }
    return m_presentationProbe->arm();
  }

  bool Output::workspaceSourceActive() const {
    return m_workspaceSourceProbe && (m_workspaceSourceProbe->active() || m_workspaceSourceProbe->preparing());
  }

  void Output::cancelPresentationProbe(PresentationFallback reason) {
    if (m_workspaceSourceProbe) {
      m_workspaceSourceProbe->cancel(reason);
    }
    if (m_presentationProbe) {
      m_presentationProbe->cancel(reason);
    }
  }

  void Output::tickPresentationProbe(uint64_t nowMsec) {
    if (m_presentationProbe) {
      m_presentationProbe->tick(nowMsec);
    }
  }

  void Output::notePresentationTopologyChange() {
    if (m_workspaceSourceProbe) {
      m_workspaceSourceProbe->cancel(PresentationFallback::TopologyChanged);
    }
    if (m_presentationProbe) {
      m_presentationProbe->topologyWillChange();
    }
  }

  void Output::completePresentationRestore() {
    if (m_workspaceSourceProbe) {
      m_workspaceSourceProbe->ordinaryCommitSucceeded();
    }
    if (m_presentationProbe) {
      m_presentationProbe->ordinaryCommitSucceeded();
    }
  }

  void Output::notePresentationLifecycle() {
    if (m_presentationProbe) {
      m_presentationProbe->nativeLifecycleStarted();
    }
  }

  bool Output::presentationProbeActive() const { return m_presentationProbe && m_presentationProbe->active(); }

  nlohmann::json Output::presentationProbeStatus() const {
    return m_presentationProbe ? m_presentationProbe->status()
                               : nlohmann::json{{"active", false}, {"reserved_bytes", 0}};
  }
} // namespace umbriel
#endif
