#pragma once

#include <cstdint>
#include <functional>

namespace umbriel {

  enum class GammaOutputMode : uint8_t {
    Sdr,
    Hdr,
  };

  struct GammaTransitionOps {
    std::function<void()> clear;
    std::function<bool()> apply;
  };

  // HDR bypasses SDR gamma tables. Returning to SDR applies the latest table,
  // with an explicit clear as the safe fallback if preparing it fails.
  [[nodiscard]] bool stageGammaForOutputMode(GammaOutputMode mode, const GammaTransitionOps& ops);

} // namespace umbriel
