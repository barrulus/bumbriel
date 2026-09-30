#include "server/ipc_commands.h"

#ifdef UMBRIEL_TEST_IPC
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"

#include <nlohmann/json.hpp>

extern "C" {
#include "../../umbrielfx/internal/types/wlr_scene.h"
}

namespace umbriel {
  nlohmann::json IpcCommands::participantAdmissionProbe(Server& server, std::string_view /*arg*/) {
    auto entries = nlohmann::json::array();
    for (const auto& view : server.views()) {
      if (!view->mapped()) {
        continue;
      }
      auto* tree = view->sceneTree();
      const bool visible = tree != nullptr && view->onActiveWorkspace() && tree->node.enabled;
      const auto admission = fx_scene_participant_admit_for_test(tree != nullptr ? &tree->node : nullptr);
      const char* detail = "supported";
      switch (admission) {
      case FX_SCENE_PARTICIPANT_SUPPORTED:
        break;
      case FX_SCENE_PARTICIPANT_IN_PLACE:
        detail = "in_place_effect";
        break;
      case FX_SCENE_PARTICIPANT_BLUR:
        detail = "backdrop_blur";
        break;
      case FX_SCENE_PARTICIPANT_TOPOLOGY:
        detail = "unsupported_topology";
        break;
      }
      entries.push_back({
          {"id", view->extForeignIdentifier() != nullptr ? view->extForeignIdentifier() : ""},
          {"title", view->toplevel()->title != nullptr ? view->toplevel()->title : ""},
          {"visible", visible},
          {"supported", visible && admission == FX_SCENE_PARTICIPANT_SUPPORTED},
          {"detail", visible ? detail : "not_visible"},
          {"border", view->effectSlot(EffectKind::Border).effectiveName()},
          {"window", view->effectSlot(EffectKind::Window).effectiveName()},
      });
    }
    return {{"ok", std::move(entries)}};
  }
} // namespace umbriel
#endif
