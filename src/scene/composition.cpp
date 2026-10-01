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
    wlr_renderer* renderer = nullptr;
    wl_listener rendererDestroy{};
    std::shared_ptr<const scene_experiment::ProgramBundle> bundle;
    std::array<Version, 2> versions;
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
    m_state->reservation = {};
    m_state.reset();
    fx_scene_release(&reservation);
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
      wl_list_remove(&state->rendererDestroy.link);
      wl_list_init(&state->rendererDestroy.link);
    };
    wl_signal_add(&renderer->events.destroy, &state->rendererDestroy);
    state->bundle = std::move(bundle);
    // Conservative row/page alignment allowance, charged for all double-buffered
    // role targets and optional final-composite scratch. No independent budget.
    const uint64_t stride = (static_cast<uint64_t>(width) * (floatingPoint ? 8 : 4) + 255) & ~uint64_t{255};
    const uint64_t image = (stride * static_cast<uint64_t>(height) + 4095) & ~uint64_t{4095};
    const uint64_t depthBytes = 0;
    const uint64_t count = 4;
    if (image > (std::numeric_limits<uint64_t>::max() - sizeof(State)) / count - depthBytes
        || !fx_scene_reserve(
            &state->reservation, &outputPool, &aggregatePool, sizeof(State) + count * (image + depthBytes)
        )) {
      return nullptr;
    }
    auto composition = std::unique_ptr<SceneComposition>(new SceneComposition(std::move(state)));

    for (auto& version : composition->m_state->versions) {
      for (auto& output : version.output) {
        if (!output.prepare(renderer, allocator, width, height, workingSpace, floatingPoint, false)) {
          return nullptr;
        }
      }
    }
    return composition;
  }

  bool SceneComposition::render(const fx_scene_frame& frame, std::span<const Source> sources) {
    auto& state = *m_state;
    if (state.renderer == nullptr) {
      return false;
    }
    if (state.pending) {
      return true; // Retry exactly the already-completed pair, with no new inputs.
    }
    if (sources.size() != 2)
      return false;
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
    auto& version = state.versions[state.candidate];
    for (unsigned role = 0; role < 2; ++role) {
      auto roleFrame = frame;
      roleFrame.role = static_cast<int>(role);
      std::array<fx_scene_input, 2> inputs{};
      inputs[0].texture = textures[0][role].get();
      inputs[1].texture = textures[1][role].get();
      inputs[0].sample_matrix = sources[0].sampleMatrix;
      inputs[1].sample_matrix = sources[1].sampleMatrix;
      if (!fx_scene_program_render(
              state.bundle->program.get(), version.output[role].target, &roleFrame, inputs.data()
          )) {
        return false;
      }
    }

    state.pending = true;
    return true;
  }

  void SceneComposition::submitted(bool success) {
    if (success && m_state->pending) {
      m_state->committed = static_cast<int>(m_state->candidate);
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

  uint64_t SceneComposition::reservedBytes() const { return m_state->reservation.bytes; }
} // namespace umbriel
