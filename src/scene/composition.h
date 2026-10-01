#pragma once

#include "scene/scene_program.h"

#include <array>
#include <memory>
#include <optional>
#include <span>

struct wlr_allocator;
struct wlr_buffer;
struct fx_scene_resource_pool;
struct fx_scene_frame;
struct fx_scene_draw;
struct fx_effect_light;

namespace umbriel {

  // Two output candidates, each containing both capture roles. A failed native
  // submission holds the completed candidate and its inputs until retry.
  class SceneComposition {
  public:
    struct Source {
      wlr_buffer* display = nullptr;
      wlr_buffer* unfiltered = nullptr;
      const float* sampleMatrix = nullptr; // optional logical UV -> imported texture UV, nine floats
      wlr_buffer* nativeShadowDisplay = nullptr;
      wlr_buffer* nativeShadowUnfiltered = nullptr;
    };
    static std::unique_ptr<SceneComposition> create(
        wlr_renderer* renderer, wlr_allocator* allocator, fx_scene_resource_pool& outputPool,
        fx_scene_resource_pool& aggregatePool, std::shared_ptr<const scene_experiment::ProgramBundle> bundle, int width,
        int height, bool workingSpace, bool floatingPoint = false
    );
    ~SceneComposition();
    // Preflight and allocate native companion scratch before lease acquisition.
    bool prepareCompanions(
        unsigned shadowPadding, unsigned lightPadding, std::span<const fx_effect_light> recipes, float scale
    );
    SceneComposition(const SceneComposition&) = delete;
    SceneComposition& operator=(const SceneComposition&) = delete;

    // Pair uses exactly two sources; geometry profiles use one per draw. All
    // draws share one immutable frame (only the capture role differs).
    bool
    render(const fx_scene_frame& frame, std::span<const Source> sources, std::span<const fx_scene_draw> draws = {});
    void submitted(bool success);
    [[nodiscard]] Source candidate() const;
    [[nodiscard]] Source committed() const;
    [[nodiscard]] bool pending() const;
    [[nodiscard]] uint64_t reservedBytes() const;
    struct Pick {
      size_t ordinal;
      float u, v;
    };
    // Coordinates and source UV belong to the last successfully submitted frame.
    [[nodiscard]] std::optional<Pick> pick(double outputLogicalX, double outputLogicalY) const;

  private:
    struct State;
    explicit SceneComposition(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
  };

} // namespace umbriel
