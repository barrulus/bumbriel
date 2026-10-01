#include "scene/composition.h"

#include "wlr.h"

extern "C" {
#include "../../umbrielfx/internal/render/fx_renderer/scene_program.h"
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace umbriel {
  struct SceneComposition::State {
    using Texture = std::unique_ptr<wlr_texture, decltype(&wlr_texture_destroy)>;
    struct PickFrame {
      fx_scene_frame frame{};
      std::vector<fx_scene_draw> draws;
      std::vector<std::array<float, 9>> matrices;
      std::vector<Texture> textures;
    };
    struct Target {
      wlr_buffer* buffer = nullptr;
      fx_scene_target* target = nullptr;
      ~Target() {
        fx_scene_target_destroy(target);
        if (buffer != nullptr) {
          wlr_buffer_drop(buffer);
        }
      }
      bool prepare(
          wlr_renderer* renderer, wlr_allocator* allocator, int width, int height, bool workingSpace,
          bool floatingPoint, bool depth
      ) {
        buffer = fx_scene_buffer_create(renderer, allocator, width, height, floatingPoint);
        target = buffer != nullptr ? fx_scene_target_create_with_color(renderer, buffer, depth, workingSpace) : nullptr;
        return target != nullptr;
      }
    };
    struct Version {
      std::array<Target, 2> output;
    };
    // Declare the reservation first so every GPU object dies before its bytes
    // are released. Destruction is explicit because pools are externally owned.
    fx_scene_reservation reservation{};
    fx_scene_reservation companions{};
    wlr_renderer* renderer = nullptr;
    wl_listener rendererDestroy{};
    std::shared_ptr<const scene_experiment::ProgramBundle> bundle;
    std::array<Version, 2> versions;
    Target intermediate;
    std::unique_ptr<fx_scene_picker, decltype(&fx_scene_picker_destroy)> picker{nullptr, fx_scene_picker_destroy};
    std::unique_ptr<PickFrame> candidatePick, committedPick;
    bool composite = false;
    bool pending = false;
    int committed = -1;
    unsigned candidate = 0;
    State() { wl_list_init(&rendererDestroy.link); }
    ~State() { wl_list_remove(&rendererDestroy.link); }
  };

  SceneComposition::SceneComposition(std::unique_ptr<State> state) : m_state(std::move(state)) {}
  SceneComposition::~SceneComposition() {
    // Keep the external pool pointers until all targets have released their
    // renderer/buffer references, including a failed unsubmitted candidate.
    auto reservation = m_state->reservation;
    auto companions = m_state->companions;
    m_state->reservation = {};
    m_state.reset();
    fx_scene_release(&reservation);
    fx_scene_release(&companions);
  }

  std::unique_ptr<SceneComposition> SceneComposition::create(
      wlr_renderer* renderer, wlr_allocator* allocator, fx_scene_resource_pool& outputPool,
      fx_scene_resource_pool& aggregatePool, std::shared_ptr<const scene_experiment::ProgramBundle> bundle, int width,
      int height, bool workingSpace, bool floatingPoint
  ) {
    if (renderer == nullptr
        || allocator == nullptr
        || bundle == nullptr
        || !bundle->program
        || width <= 0
        || height <= 0) {
      return nullptr;
    }
    fx_scene_limits limits{};
    if (!fx_scene_program_get_limits(renderer, &limits)
        || static_cast<unsigned>(width) > limits.texture_size
        || static_cast<unsigned>(height) > limits.texture_size
        || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > FX_SCENE_OUTPUT_BUDGET / 4) {
      return nullptr;
    }
    auto state = std::make_unique<State>();
    state->renderer = renderer;
    state->rendererDestroy.notify = [](wl_listener* listener, void*) {
      State* state;
      state = wl_container_of(listener, state, rendererDestroy);
      state->renderer = nullptr;
      state->pending = false;
      state->committed = -1;
      state->candidatePick.reset();
      state->committedPick.reset();
      wl_list_remove(&state->rendererDestroy.link);
      wl_list_init(&state->rendererDestroy.link);
    };
    wl_signal_add(&renderer->events.destroy, &state->rendererDestroy);
    state->bundle = std::move(bundle);
    state->composite =
        state->bundle->definition.sources.stages[static_cast<size_t>(scene_experiment::Stage::Composite)].has_value();
    const bool depth = state->bundle->definition.sources.scope == scene_experiment::Scope::WorkspaceSet;
    // Conservative row/page alignment allowance, charged for all double-buffered
    // role targets and optional final-composite scratch. No independent budget.
    const uint64_t stride = (static_cast<uint64_t>(width) * (floatingPoint ? 8 : 4) + 255) & ~uint64_t{255};
    const uint64_t image = (stride * static_cast<uint64_t>(height) + 4095) & ~uint64_t{4095};
    const uint64_t depthBytes = depth ? static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 2 : 0;
    const uint64_t count = state->composite ? 5 : 4;
    const bool picking = fx_scene_program_supports_picking(state->bundle->program.get());
    const uint64_t pickBytes = picking ? FX_SCENE_PICKER_BYTES
            + 2
                * (sizeof(State::PickFrame)
                   + FX_SCENE_MAX_DRAWS
                       * (sizeof(fx_scene_draw)
                          + sizeof(std::array<float, 9>)
                          + sizeof(State::Texture)
                          + FX_SCENE_INPUT_METADATA_BYTES))
                                       : 0;
    if (image > (std::numeric_limits<uint64_t>::max() - sizeof(State)) / count - depthBytes
        || !fx_scene_reserve(
            &state->reservation, &outputPool, &aggregatePool, sizeof(State) + count * (image + depthBytes) + pickBytes
        )) {
      return nullptr;
    }
    auto composition = std::unique_ptr<SceneComposition>(new SceneComposition(std::move(state)));
    if (picking) {
      composition->m_state->picker.reset(fx_scene_picker_create(renderer));
      if (!composition->m_state->picker)
        return nullptr;
    }
    for (auto& version : composition->m_state->versions) {
      for (auto& output : version.output) {
        if (!output.prepare(renderer, allocator, width, height, workingSpace, floatingPoint, depth)) {
          return nullptr;
        }
      }
    }
    // The optional final composite consumes this scratch before the next role
    // begins. Only completed output images need independent retry versions.
    if (composition->m_state->composite
        && !composition->m_state->intermediate.prepare(
            renderer, allocator, width, height, workingSpace, floatingPoint, depth
        )) {
      return nullptr;
    }
    return composition;
  }

  bool SceneComposition::prepareCompanions(
      unsigned shadowPadding, unsigned lightPadding, std::span<const fx_effect_light> recipes, float scale
  ) {
    auto& state = *m_state;
    const unsigned commonPadding = std::max(shadowPadding, lightPadding);
    if (shadowPadding > 0)
      shadowPadding = commonPadding;
    if (!recipes.empty())
      lightPadding = commonPadding;
    if (state.companions.bytes != 0 || state.pending || state.committed >= 0) {
      return false;
    }
    if (shadowPadding == 0 && recipes.empty()) {
      return true;
    }
    auto* owner = state.composite ? state.intermediate.target : state.versions.front().output.front().target;
    // Roles and versions are rendered sequentially. Their completed images stay
    // separate, but one scratch set covers every companion pass. Light storage
    // already includes the three images also used by the shadow pass.
    const uint64_t bytes = !recipes.empty()
        ? fx_scene_target_light_bytes(owner, lightPadding, recipes.data(), static_cast<unsigned>(recipes.size()), scale)
        : fx_scene_target_shadow_bytes(owner, shadowPadding);
    if (bytes == 0
        || !fx_scene_reserve(&state.companions, state.reservation.output, state.reservation.aggregate, bytes)) {
      return false;
    }
    if ((!recipes.empty()
         && !fx_scene_target_prepare_light(
             owner, lightPadding, recipes.data(), static_cast<unsigned>(recipes.size()), scale
         ))
        || (recipes.empty() && !fx_scene_target_prepare_shadow(owner, shadowPadding))) {
      return false;
    }
    if (!state.composite) {
      for (auto& version : state.versions) {
        for (auto& target : version.output) {
          if (target.target != owner && !fx_scene_target_share_scratch(target.target, owner)) {
            return false;
          }
        }
      }
    }
    return true;
  }

  bool SceneComposition::render(
      const fx_scene_frame& frame, std::span<const Source> sources, std::span<const fx_scene_draw> draws
  ) {
    auto& state = *m_state;
    if (state.renderer == nullptr) {
      return false;
    }
    if (state.pending) {
      return true; // Retry exactly the already-completed pair, with no new inputs.
    }
    const bool pair = state.bundle->definition.sources.scope == scene_experiment::Scope::WorkspacePair;
    if ((pair && (sources.size() != 2 || !draws.empty()))
        || (!pair && (sources.empty() || sources.size() != draws.size() || draws.size() > FX_SCENE_MAX_DRAWS))) {
      return false;
    }
    using Texture = std::unique_ptr<wlr_texture, decltype(&wlr_texture_destroy)>;
    std::vector<std::array<Texture, 2>> textures;
    textures.reserve(sources.size());
    for (const auto& source : sources) {
      if (source.display == nullptr || source.unfiltered == nullptr) {
        return false;
      }
      std::array<Texture, 2> imported{
          Texture(wlr_texture_from_buffer(state.renderer, source.display), wlr_texture_destroy),
          Texture(wlr_texture_from_buffer(state.renderer, source.unfiltered), wlr_texture_destroy)
      };
      if (!imported[0] || !imported[1]) {
        return false;
      }
      textures.push_back(std::move(imported));
    }
    std::vector<std::array<Texture, 2>> nativeShadows;
    nativeShadows.reserve(sources.size());
    for (const auto& source : sources) {
      std::array<Texture, 2> imported{
          Texture(
              source.nativeShadowDisplay ? wlr_texture_from_buffer(state.renderer, source.nativeShadowDisplay)
                                         : nullptr,
              wlr_texture_destroy
          ),
          Texture(
              source.nativeShadowUnfiltered ? wlr_texture_from_buffer(state.renderer, source.nativeShadowUnfiltered)
                                            : nullptr,
              wlr_texture_destroy
          )
      };
      if ((source.nativeShadowDisplay && !imported[0]) || (source.nativeShadowUnfiltered && !imported[1])) {
        return false;
      }
      nativeShadows.push_back(std::move(imported));
    }
    auto& version = state.versions[state.candidate];
    std::vector<fx_scene_shadow> roleShadows(draws.size());
    std::vector<fx_scene_draw> roleDraws(draws.begin(), draws.end());
    for (unsigned role = 0; role < 2; ++role) {
      auto roleFrame = frame;
      roleFrame.role = static_cast<int>(role);
      std::array<fx_scene_input, 2> inputs{};
      if (pair) {
        inputs[0].texture = textures[0][role].get();
        inputs[1].texture = textures[1][role].get();
        inputs[0].sample_matrix = sources[0].sampleMatrix;
        inputs[1].sample_matrix = sources[1].sampleMatrix;
      } else {
        for (size_t i = 0; i < draws.size(); ++i) {
          roleDraws[i].input.texture = textures[i][role].get();
          roleDraws[i].input.sample_matrix = sources[i].sampleMatrix;
          if (draws[i].shadow != nullptr) {
            roleShadows[i] = *draws[i].shadow;
            roleShadows[i].native_texture = nativeShadows[i][role].get();
            roleDraws[i].shadow = &roleShadows[i];
          }
        }
      }
      if (!fx_scene_program_render(
              state.bundle->program.get(), version.output[role].target,
              state.composite ? state.intermediate.target : nullptr, &roleFrame, pair ? inputs.data() : nullptr,
              pair ? nullptr : roleDraws.data(), static_cast<unsigned>(roleDraws.size())
          )) {
        return false;
      }
    }
    if (state.picker) {
      auto snapshot = std::make_unique<State::PickFrame>();
      snapshot->frame = frame;
      snapshot->frame.role = 0;
      snapshot->draws.assign(draws.begin(), draws.end());
      snapshot->matrices.resize(draws.size());
      snapshot->textures.reserve(draws.size());
      for (size_t i = 0; i < draws.size(); ++i) {
        snapshot->textures.push_back(std::move(textures[i][0]));
        snapshot->draws[i].input.texture = snapshot->textures.back().get();
        snapshot->draws[i].input.sample_matrix = nullptr;
        if (sources[i].sampleMatrix) {
          std::copy_n(sources[i].sampleMatrix, 9, snapshot->matrices[i].begin());
          snapshot->draws[i].input.sample_matrix = snapshot->matrices[i].data();
        }
        // Set-profile meshes are immutable until their composition is destroyed.
        snapshot->draws[i].shadow = nullptr;
        snapshot->draws[i].light = nullptr;
      }
      state.candidatePick = std::move(snapshot);
    }
    state.pending = true;
    return true;
  }

  void SceneComposition::submitted(bool success) {
    if (success && m_state->pending) {
      m_state->committed = static_cast<int>(m_state->candidate);
      m_state->committedPick = std::move(m_state->candidatePick);
      m_state->candidate ^= 1;
      m_state->pending = false;
    }
  }
  SceneComposition::Source SceneComposition::candidate() const {
    if (!m_state->pending) {
      return {};
    }
    const auto& version = m_state->versions[m_state->candidate];
    return {version.output[0].buffer, version.output[1].buffer};
  }
  SceneComposition::Source SceneComposition::committed() const {
    if (m_state->committed < 0) {
      return {};
    }
    const auto& version = m_state->versions[static_cast<unsigned>(m_state->committed)];
    return {version.output[0].buffer, version.output[1].buffer};
  }
  bool SceneComposition::pending() const { return m_state->pending; }
  std::optional<SceneComposition::Pick> SceneComposition::pick(double x, double y) const {
    auto& state = *m_state;
    if (!state.renderer
        || !state.picker
        || !state.committedPick
        || state.committed < 0
        || !std::isfinite(x)
        || !std::isfinite(y))
      return std::nullopt;
    const auto& snapshot = *state.committedPick;
    fx_scene_pick result{};
    if (fx_scene_program_pick(
            state.bundle->program.get(), state.picker.get(),
            state.versions[static_cast<unsigned>(state.committed)].output[0].target, &snapshot.frame,
            snapshot.draws.data(), static_cast<unsigned>(snapshot.draws.size()), static_cast<float>(x),
            static_cast<float>(y), &result
        ) != FX_SCENE_PICK_HIT
        || result.ordinal < 0)
      return std::nullopt;
    return Pick{static_cast<size_t>(result.ordinal), result.uv[0], result.uv[1]};
  }
  uint64_t SceneComposition::reservedBytes() const { return m_state->reservation.bytes + m_state->companions.bytes; }
} // namespace umbriel
