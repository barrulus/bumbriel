#pragma once

#ifdef UMBRIEL_TEST_IPC
#include "scene/presentation.h"

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <vector>

struct fx_scene_resource_pool;

namespace umbriel {
  fx_scene_resource_pool& presentationAggregatePool();
  class Output;
  class Server;

  // C0 fixture owner, never a configured scene feature. It borrows an existing
  // native lifecycle clock and replaces only this output's renderer entries.
  class PresentationProbe {
  public:
    PresentationProbe(Server& server, Output& output);
    ~PresentationProbe();
    bool arm();
    void cancel(PresentationFallback reason);
    void tick(uint64_t nowMsec);
    void nativeLifecycleStarted();
    void topologyWillChange();
    void ordinaryCommitSucceeded();
    [[nodiscard]] bool active() const;
    [[nodiscard]] nlohmann::json status() const;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace umbriel
#endif
