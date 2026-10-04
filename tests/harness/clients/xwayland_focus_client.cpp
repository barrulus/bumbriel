// Maps a managed X11 window and reports both forms of X focus. Commands on
// stdin use `state LABEL`; each produces one line after a server round trip.

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <poll.h>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <xcb/xcb.h>

namespace {
  struct Atoms {
    xcb_atom_t utf8String = XCB_ATOM_NONE;
    xcb_atom_t netWmName = XCB_ATOM_NONE;
    xcb_atom_t netActiveWindow = XCB_ATOM_NONE;
    xcb_atom_t wmProtocols = XCB_ATOM_NONE;
    xcb_atom_t wmDeleteWindow = XCB_ATOM_NONE;
  };

  struct State {
    xcb_connection_t* connection = nullptr;
    xcb_screen_t* screen = nullptr;
    Atoms atoms;
    std::array<xcb_window_t, 1> windows{};
    std::array<std::string, 1> names;
    std::string commands;
    bool running = true;
  };

  xcb_atom_t internAtom(xcb_connection_t* connection, std::string_view name) {
    const xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(connection, false, static_cast<uint16_t>(name.size()), name.data());
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
      return XCB_ATOM_NONE;
    }
    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
  }

  bool loadAtoms(State& state) {
    state.atoms.utf8String = internAtom(state.connection, "UTF8_STRING");
    state.atoms.netWmName = internAtom(state.connection, "_NET_WM_NAME");
    state.atoms.netActiveWindow = internAtom(state.connection, "_NET_ACTIVE_WINDOW");
    state.atoms.wmProtocols = internAtom(state.connection, "WM_PROTOCOLS");
    state.atoms.wmDeleteWindow = internAtom(state.connection, "WM_DELETE_WINDOW");
    return state.atoms.utf8String != XCB_ATOM_NONE
        && state.atoms.netWmName != XCB_ATOM_NONE
        && state.atoms.netActiveWindow != XCB_ATOM_NONE
        && state.atoms.wmProtocols != XCB_ATOM_NONE
        && state.atoms.wmDeleteWindow != XCB_ATOM_NONE;
  }

  std::string windowName(const State& state, xcb_window_t window) {
    if (window == XCB_NONE) {
      return "none";
    }
    if (window == XCB_INPUT_FOCUS_POINTER_ROOT) {
      return "pointer-root";
    }
    for (size_t index = 0; index < state.windows.size(); ++index) {
      if (window == state.windows[index]) {
        return state.names[index];
      }
    }
    return std::format("other:0x{:x}", window);
  }

  std::optional<xcb_window_t> activeWindow(const State& state) {
    const xcb_get_property_cookie_t cookie = xcb_get_property(
        state.connection, false, state.screen->root, state.atoms.netActiveWindow, XCB_ATOM_WINDOW, 0, 1
    );
    xcb_get_property_reply_t* reply = xcb_get_property_reply(state.connection, cookie, nullptr);
    if (reply == nullptr) {
      return std::nullopt;
    }
    std::optional<xcb_window_t> window;
    if (reply->type == XCB_ATOM_WINDOW
        && reply->format == 32
        && xcb_get_property_value_length(reply) >= static_cast<int>(sizeof(xcb_window_t))) {
      xcb_window_t value = XCB_NONE;
      std::memcpy(&value, xcb_get_property_value(reply), sizeof(value));
      window = value;
    }
    std::free(reply);
    return window;
  }

  void reportState(const State& state, std::string_view label) {
    const xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(state.connection);
    xcb_get_input_focus_reply_t* reply = xcb_get_input_focus_reply(state.connection, cookie, nullptr);
    const xcb_window_t input = reply != nullptr ? reply->focus : XCB_NONE;
    std::free(reply);
    const std::optional<xcb_window_t> active = activeWindow(state);
    std::println(
        "state label={} input={} active={}", label, windowName(state, input),
        active.has_value() ? windowName(state, *active) : "missing"
    );
  }

  void handleCommand(State& state, std::string_view command) {
    constexpr std::string_view prefix = "state ";
    if (command.starts_with(prefix) && command.size() > prefix.size()) {
      reportState(state, command.substr(prefix.size()));
    } else if (command == "quit") {
      state.running = false;
    } else if (!command.empty()) {
      std::println(stderr, "xwayland-focus-client: unknown command '{}'", command);
      state.running = false;
    }
  }

  void readCommands(State& state) {
    std::array<char, 1024> buffer{};
    const ssize_t count = read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count == 0) {
      state.running = false;
      return;
    }
    if (count < 0) {
      if (errno != EAGAIN && errno != EINTR) {
        std::println(stderr, "xwayland-focus-client: stdin read failed: {}", std::strerror(errno));
        state.running = false;
      }
      return;
    }
    state.commands.append(buffer.data(), static_cast<size_t>(count));
    size_t newline = std::string::npos;
    while ((newline = state.commands.find('\n')) != std::string::npos) {
      const std::string command = state.commands.substr(0, newline);
      state.commands.erase(0, newline + 1);
      handleCommand(state, command);
    }
  }

  void handleEvent(State& state, const xcb_generic_event_t& event) {
    const uint8_t type = event.response_type & static_cast<uint8_t>(~0x80U);
    if (type == XCB_FOCUS_IN || type == XCB_FOCUS_OUT) {
      const auto& focus = reinterpret_cast<const xcb_focus_in_event_t&>(event);
      std::println("focus {} window={}", type == XCB_FOCUS_IN ? "in" : "out", windowName(state, focus.event));
      return;
    }
    if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
      const auto& key = reinterpret_cast<const xcb_key_press_event_t&>(event);
      std::println(
          "key window={} code={} state={}", windowName(state, key.event), key.detail,
          type == XCB_KEY_PRESS ? "pressed" : "released"
      );
      return;
    }
    if (type == XCB_CLIENT_MESSAGE) {
      const auto& message = reinterpret_cast<const xcb_client_message_event_t&>(event);
      if (message.type == state.atoms.wmProtocols && message.data.data32[0] == state.atoms.wmDeleteWindow) {
        state.running = false;
      }
    }
  }

  bool createWindow(State& state, size_t index, int16_t x) {
    const xcb_window_t window = xcb_generate_id(state.connection);
    state.windows[index] = window;
    constexpr uint32_t eventMask = XCB_EVENT_MASK_STRUCTURE_NOTIFY
        | XCB_EVENT_MASK_FOCUS_CHANGE
        | XCB_EVENT_MASK_KEY_PRESS
        | XCB_EVENT_MASK_KEY_RELEASE;
    const uint32_t values[] = {state.screen->black_pixel, eventMask};
    xcb_create_window(
        state.connection, XCB_COPY_FROM_PARENT, window, state.screen->root, x, 80, 480, 360, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, state.screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values
    );

    const std::string& name = state.names[index];
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        static_cast<uint32_t>(name.size()), name.data()
    );
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, window, state.atoms.netWmName, state.atoms.utf8String, 8,
        static_cast<uint32_t>(name.size()), name.data()
    );
    const std::string wmClass = name + '\0' + "UmbrielXwaylandFocus" + '\0';
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
        static_cast<uint32_t>(wmClass.size()), wmClass.data()
    );
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, window, state.atoms.wmProtocols, XCB_ATOM_ATOM, 32, 1,
        &state.atoms.wmDeleteWindow
    );
    xcb_map_window(state.connection, window);
    return xcb_connection_has_error(state.connection) == 0;
  }
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(stderr, "usage: xwayland-focus-client TITLE");
    return 2;
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);

  int screenNumber = 0;
  State state;
  state.connection = xcb_connect(nullptr, &screenNumber);
  state.names = {argv[1]};
  if (state.connection == nullptr || xcb_connection_has_error(state.connection) != 0) {
    std::println(stderr, "xwayland-focus-client: failed to connect to DISPLAY");
    return 1;
  }

  const xcb_setup_t* setup = xcb_get_setup(state.connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  for (int index = 0; index < screenNumber && screens.rem != 0; ++index) {
    xcb_screen_next(&screens);
  }
  state.screen = screens.data;
  if (state.screen == nullptr || !loadAtoms(state) || !createWindow(state, 0, 80)) {
    std::println(stderr, "xwayland-focus-client: failed to create windows");
    xcb_disconnect(state.connection);
    return 1;
  }
  xcb_flush(state.connection);
  std::println("ready window=0x{:x}", state.windows[0]);

  const int xcbFd = xcb_get_file_descriptor(state.connection);
  while (state.running && xcb_connection_has_error(state.connection) == 0) {
    const std::array<pollfd, 2> descriptors{{
        {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
        {.fd = xcbFd, .events = POLLIN, .revents = 0},
    }};
    auto ready = descriptors;
    const int result = poll(ready.data(), ready.size(), -1);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::println(stderr, "xwayland-focus-client: poll failed: {}", std::strerror(errno));
      break;
    }
    if ((ready[0].revents & (POLLIN | POLLHUP)) != 0) {
      readCommands(state);
    }
    if ((ready[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      while (xcb_generic_event_t* event = xcb_poll_for_event(state.connection)) {
        handleEvent(state, *event);
        std::free(event);
      }
    }
  }

  for (xcb_window_t window : state.windows) {
    xcb_destroy_window(state.connection, window);
  }
  xcb_disconnect(state.connection);
  return state.running ? 1 : 0;
}
