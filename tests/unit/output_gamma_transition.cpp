#include "check.h"
#include "output/gamma_transition.h"

using umbriel::GammaOutputMode;
using umbriel::GammaTransitionOps;
using umbriel::stageGammaForOutputMode;

namespace {

  struct Recorder {
    int clears = 0;
    int applies = 0;
    bool applyResult = true;

    GammaTransitionOps ops() {
      return GammaTransitionOps{
          .clear = [this] { ++clears; },
          .apply =
              [this] {
                ++applies;
                return applyResult;
              },
      };
    }
  };

} // namespace

UMBRIEL_TEST(hdrClearsGammaWithoutApplyingIt) {
  Recorder recorder;

  CHECK(stageGammaForOutputMode(GammaOutputMode::Hdr, recorder.ops()));
  CHECK_EQ(recorder.clears, 1);
  CHECK_EQ(recorder.applies, 0);
}

UMBRIEL_TEST(sdrAppliesLatestGammaWithoutClearingIt) {
  Recorder recorder;

  CHECK(stageGammaForOutputMode(GammaOutputMode::Sdr, recorder.ops()));
  CHECK_EQ(recorder.clears, 0);
  CHECK_EQ(recorder.applies, 1);
}

UMBRIEL_TEST(sdrClearsGammaWhenPreparingTheTableFails) {
  Recorder recorder;
  recorder.applyResult = false;

  CHECK(!stageGammaForOutputMode(GammaOutputMode::Sdr, recorder.ops()));
  CHECK_EQ(recorder.clears, 1);
  CHECK_EQ(recorder.applies, 1);
}

int main() { return RUN_TESTS(); }
