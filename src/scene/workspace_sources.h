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
  };

  enum class WorkspaceSourceResult { Preparing, Ready, Failed };

  // Internal native workspace inventory and paired source owner. The caller
  // owns composition and final output replacement.
  class WorkspaceSources {
  public:
    WorkspaceSources(Server& server, Output& output, std::function<void(PresentationFallback)> invalidated = {});
    ~WorkspaceSources();
    // Retains complete native identity ownership, but captures exactly the two
    // native-resolution faces required by a workspace transition.
    bool beginPair(std::string_view from, std::string_view to);
    bool freezeOutgoing();
    bool activateSelection(std::string_view identity);
    WorkspaceSourceResult prepareFrame(bool animate);
    [[nodiscard]] std::span<const WorkspaceSourceFace> faces() const;
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
    // Final composition reserves output targets in this same output
    // arena and presentationAggregatePool before acquisition. Release those
    // reservations before destroying this provider.
    [[nodiscard]] fx_scene_resource_pool& resourcePool();

    void tick(bool animate);
    void frameCommitted();
    [[nodiscard]] nlohmann::json status() const;

  private:
    bool open(std::string_view identity);
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace umbriel
