#include "server/ipc_commands.h"

#ifdef UMBRIEL_TEST_IPC
#include "input/cursor.h"
#include "server/server.h"
#include "wlr.h"

extern "C" {
#include <wlr/interfaces/wlr_pointer.h>
}

#include <cmath>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>

namespace umbriel {
  namespace {
    struct SwipeDevice;
    std::map<Server*, std::unique_ptr<SwipeDevice>> devices;
    const wlr_pointer_impl implementation{.name = "umbriel-test-swipe"};

    struct SwipeDevice {
      Server* server;
      wlr_pointer pointer{};
      wl_listener displayDestroy{};
      uint32_t fingers = 0;
      uint32_t time = 0;
      bool active = false;
      explicit SwipeDevice(Server& owner) : server(&owner) {
        wlr_pointer_init(&pointer, &implementation, "umbriel-test-swipe");
        displayDestroy.notify = [](wl_listener* listener, void*) {
          SwipeDevice* self;
          self = wl_container_of(listener, self, displayDestroy);
          devices.erase(self->server);
        };
        wl_display_add_destroy_listener(owner.display(), &displayDestroy);
      }
      ~SwipeDevice() {
        wl_list_remove(&displayDestroy.link);
        wlr_pointer_finish(&pointer);
      }
    };
  } // namespace

  nlohmann::json IpcCommands::swipeInject(Server& server, std::string_view arg) {
    std::istringstream input{std::string(arg)};
    std::string operation;
    input >> operation;
    if (operation == "remove") {
      devices.erase(&server);
      return {{"ok", true}};
    }
    uint32_t time = 0;
    uint32_t fingers = 0;
    double dx = 0;
    double dy = 0;
    if (operation == "begin") {
      if (!(input >> fingers >> time) || fingers == 0 || fingers > 10) {
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
      return {{"err", "expected begin|update|end|cancel|remove"}};
    }
    std::string extra;
    if (input >> extra) {
      return {{"err", "unexpected swipe arguments"}};
    }
    const auto found = devices.find(&server);
    if (operation != "begin" && (found == devices.end() || !found->second->active)) {
      return {{"err", "no active swipe"}};
    }
    if (operation == "begin" && found != devices.end() && found->second->active) {
      return {{"err", "swipe already active"}};
    }
    auto& stored = devices[&server];
    if (!stored) {
      stored = std::make_unique<SwipeDevice>(server);
    }
    auto& device = *stored;
    if (operation != "begin" && time < device.time) {
      return {{"err", "swipe time must not go backwards"}};
    }
    device.time = time;
    auto* cursor = server.cursor()->wlr();
    if (operation == "begin") {
      device.fingers = fingers;
      device.active = true;
      wlr_pointer_swipe_begin_event event{.pointer = &device.pointer, .time_msec = time, .fingers = fingers};
      wl_signal_emit_mutable(&cursor->events.swipe_begin, &event);
    } else if (operation == "update") {
      wlr_pointer_swipe_update_event event{
          .pointer = &device.pointer, .time_msec = time, .fingers = device.fingers, .dx = dx, .dy = dy
      };
      wl_signal_emit_mutable(&cursor->events.swipe_update, &event);
    } else {
      device.active = false;
      wlr_pointer_swipe_end_event event{
          .pointer = &device.pointer, .time_msec = time, .cancelled = operation == "cancel"
      };
      wl_signal_emit_mutable(&cursor->events.swipe_end, &event);
    }
    return {{"ok", true}};
  }
} // namespace umbriel
#endif
