#include "output/gamma_transition.h"

namespace umbriel {

  bool stageGammaForOutputMode(GammaOutputMode mode, const GammaTransitionOps& ops) {
    if (mode == GammaOutputMode::Hdr) {
      ops.clear();
      return true;
    }
    if (ops.apply()) {
      return true;
    }
    ops.clear();
    return false;
  }

} // namespace umbriel
