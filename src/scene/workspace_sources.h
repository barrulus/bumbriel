#pragma once

#include "scene/presentation.h"

#include <array>
#include <functional>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <wlr/util/box.h>
}

struct timespec;
struct wlr_buffer;
struct fx_scene_resource_pool;

namespace umbriel {
  class Output;
  class Server;
  class View;

  fx_scene_resource_pool& presentationAggregatePool();

  // Borrowed immutable role images. Valid until the next prepareFrame,
  // frameSubmitted(true), or cancellation; the provider owns their buffers.
  struct WorkspaceSourceFace {
    std::string identity;
    wlr_buffer* display = nullptr;
    wlr_buffer* unfiltered = nullptr;
    int width = 0;
    int height = 0;
    bool workingSpace = false;
    bool floatingPoint = false;
    wlr_box viewport{};
    wlr_box contentBounds{};
    wlr_box extent{};
    std::array<float, 4> contentFraming{};
    std::array<float, 4> viewportFraming{};
  };

  enum class WorkspaceSourceResult { Preparing, Ready, Failed };

  // Internal native workspace inventory and paired source owner. The caller
  // owns mesh composition, input admission and final output replacement. Every
  // displayed face shares the caller output's existing immutable audio latch.
  class WorkspaceSources {
  public:
    WorkspaceSources(
        Server& server, Output& output, std::function<void(PresentationFallback)> invalidated = {},
        bool previewForTest = false
    );
    ~WorkspaceSources();
    bool begin(std::string_view identity);
    // Retains complete native identity ownership, but captures exactly the two
    // native-resolution faces required by a workspace transition.
    bool beginPair(std::string_view from, std::string_view to);
    bool freezeOutgoing();
    bool activateSelection(std::string_view identity);
    bool commitSelection(std::string_view identity);
    bool setVisibleWorkspaces(std::span<const std::string> identities);
    WorkspaceSourceResult prepareFrame(bool animate);
    [[nodiscard]] std::vector<WorkspaceSourceFace> faces() const;
    struct Pick {
      std::string identity;
      std::string window;
    };
    [[nodiscard]] std::optional<Pick> pick(size_t ordinal, float u, float v) const;
    // Adds a separate native-resolution viewport pair to the complete resource
    // preflight. It never reuses a reduced-resolution face for native landing.
    bool requestLanding(std::string_view identity);
    bool setFramingProgress(float progress);
    bool setLandingMix(float weight);
    [[nodiscard]] std::optional<WorkspaceSourceFace> landing() const;
    void frameSubmitted(bool success);
    void sendFrameDone(const timespec& when);
    void cancel(PresentationFallback reason);
    void contentChanged();
    void viewMapped(View& view);
    void viewWillUnmap(View& view);
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool preparing() const;
    [[nodiscard]] bool renderLocked() const;
    [[nodiscard]] uint64_t reservedBytes() const;
    [[nodiscard]] uint64_t revision() const;
    [[nodiscard]] PresentationFallback lastFallback() const;
    // Final composition reserves target/depth/landing in this same output
    // arena and presentationAggregatePool before acquisition. Release those
    // reservations before destroying this provider.
    [[nodiscard]] fx_scene_resource_pool& resourcePool();

    // Single-face C0 preview adapter. Production callers use the API above.
    bool open(std::string_view identity);
    bool select(std::string_view identity);
    void tick(bool animate);
    void frameCommitted();
    void ordinaryCommitSucceeded();
    [[nodiscard]] nlohmann::json status() const;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace umbriel
