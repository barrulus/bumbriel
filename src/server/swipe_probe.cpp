#include "server/ipc_commands.h"

#ifdef UMBRIEL_TEST_IPC
#include "input/cursor.h"
#include "server/server.h"
#include "wlr.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>

namespace umbriel {
  nlohmann::json IpcCommands::swipeInject(Server& server, std::string_view arg) {
    std::istringstream input{std::string(arg)};
    std::string operation;
    input >> operation;

    uint32_t time = 0;
    uint32_t fingers = 0;
    double dx = 0;
    double dy = 0;
    if (operation == "begin") {
      if (!(input >> fingers >> time) || fingers != 3) {
        return {{"err", "expected begin fingers time_ms"}};
      }
    } else if (operation == "update") {
      if (!(input >> dx >> dy >> time) || !std::isfinite(dx) || !std::isfinite(dy)) {
        return {{"err", "expected update dx dy time_ms"}};
      }
    } else if (operation == "end" || operation == "cancel") {
      if (!(input >> time)) {
        return {{"err", "expected end|cancel time_ms"}};
      }
    } else {
      return {{"err", "expected begin|update|end|cancel"}};
    }
    std::string extra;
    if (input >> extra) {
      return {{"err", "unexpected swipe arguments"}};
    }
    auto* cursor = server.cursor()->wlr();
    if (operation == "begin") {
      wlr_pointer_swipe_begin_event event{.pointer = nullptr, .time_msec = time, .fingers = fingers};
      wl_signal_emit_mutable(&cursor->events.swipe_begin, &event);
    } else if (operation == "update") {
      wlr_pointer_swipe_update_event event{.pointer = nullptr, .time_msec = time, .fingers = 3, .dx = dx, .dy = dy};
      wl_signal_emit_mutable(&cursor->events.swipe_update, &event);
    } else {
      wlr_pointer_swipe_end_event event{.pointer = nullptr, .time_msec = time, .cancelled = operation == "cancel"};
      wl_signal_emit_mutable(&cursor->events.swipe_end, &event);
    }
    return {{"ok", true}};
  }
} // namespace umbriel
#endif
