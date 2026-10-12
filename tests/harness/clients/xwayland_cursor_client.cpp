// Maps an X11 window and switches its cursor between an exact image from the
// current Xcursor theme, the same pixels with a different hotspot, and a
// same-sized image whose pixels cannot match that theme. Commands on stdin are
// `theme`, `hotspot`, `custom`, and `quit`.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <poll.h>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>
extern "C" {
#include <wlr/xcursor.h>
}
#include <xcb/render.h>
#include <xcb/xcb.h>

namespace {
  constexpr int kCursorSize = 24;

  struct Atoms {
    xcb_atom_t utf8String = XCB_ATOM_NONE;
    xcb_atom_t netWmName = XCB_ATOM_NONE;
    xcb_atom_t wmProtocols = XCB_ATOM_NONE;
    xcb_atom_t wmDeleteWindow = XCB_ATOM_NONE;
  };

  struct CursorImage {
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t hotspotX = 0;
    uint16_t hotspotY = 0;
    std::vector<uint32_t> pixels;
  };

  struct State {
    xcb_connection_t* connection = nullptr;
    xcb_screen_t* screen = nullptr;
    xcb_window_t window = XCB_NONE;
    xcb_cursor_t themeCursor = XCB_NONE;
    xcb_cursor_t hotspotCursor = XCB_NONE;
    xcb_cursor_t customCursor = XCB_NONE;
    Atoms atoms;
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
    state.atoms.wmProtocols = internAtom(state.connection, "WM_PROTOCOLS");
    state.atoms.wmDeleteWindow = internAtom(state.connection, "WM_DELETE_WINDOW");
    return state.atoms.utf8String != XCB_ATOM_NONE
        && state.atoms.netWmName != XCB_ATOM_NONE
        && state.atoms.wmProtocols != XCB_ATOM_NONE
        && state.atoms.wmDeleteWindow != XCB_ATOM_NONE;
  }

  bool checkRequest(xcb_connection_t* connection, xcb_void_cookie_t cookie, std::string_view operation) {
    xcb_generic_error_t* error = xcb_request_check(connection, cookie);
    if (error == nullptr) {
      return true;
    }
    std::println(stderr, "xwayland-cursor-client: {} failed with X11 error {}", operation, error->error_code);
    std::free(error);
    return false;
  }

  xcb_render_pictformat_t argbFormat(xcb_connection_t* connection) {
    const xcb_render_query_pict_formats_cookie_t cookie = xcb_render_query_pict_formats(connection);
    xcb_render_query_pict_formats_reply_t* reply = xcb_render_query_pict_formats_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
      return XCB_NONE;
    }
    xcb_render_pictformat_t result = XCB_NONE;
    xcb_render_pictforminfo_iterator_t iterator = xcb_render_query_pict_formats_formats_iterator(reply);
    for (; iterator.rem != 0; xcb_render_pictforminfo_next(&iterator)) {
      const xcb_render_pictforminfo_t& format = *iterator.data;
      const xcb_render_directformat_t& direct = format.direct;
      if (format.type == XCB_RENDER_PICT_TYPE_DIRECT
          && format.depth == 32
          && direct.red_shift == 16
          && direct.red_mask == 0xFF
          && direct.green_shift == 8
          && direct.green_mask == 0xFF
          && direct.blue_shift == 0
          && direct.blue_mask == 0xFF
          && direct.alpha_shift == 24
          && direct.alpha_mask == 0xFF) {
        result = format.id;
        break;
      }
    }
    std::free(reply);
    return result;
  }

  xcb_cursor_t createCursor(
      xcb_connection_t* connection, xcb_screen_t* screen, xcb_render_pictformat_t format, const CursorImage& image
  ) {
    const xcb_pixmap_t pixmap = xcb_generate_id(connection);
    const xcb_gcontext_t graphics = xcb_generate_id(connection);
    const xcb_render_picture_t picture = xcb_generate_id(connection);
    const xcb_cursor_t cursor = xcb_generate_id(connection);

    if (!checkRequest(
            connection, xcb_create_pixmap_checked(connection, 32, pixmap, screen->root, image.width, image.height),
            "creating a 32-bit cursor pixmap"
        )
        || !checkRequest(
            connection, xcb_create_gc_checked(connection, graphics, pixmap, 0, nullptr),
            "creating a cursor graphics context"
        )
        || !checkRequest(
            connection,
            xcb_put_image_checked(
                connection, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, graphics, image.width, image.height, 0, 0, 0, 32,
                static_cast<uint32_t>(image.pixels.size() * sizeof(uint32_t)),
                reinterpret_cast<const uint8_t*>(image.pixels.data())
            ),
            "uploading cursor pixels"
        )
        || !checkRequest(
            connection, xcb_render_create_picture_checked(connection, picture, pixmap, format, 0, nullptr),
            "creating a cursor picture"
        )
        || !checkRequest(
            connection, xcb_render_create_cursor_checked(connection, cursor, picture, image.hotspotX, image.hotspotY),
            "creating an ARGB cursor"
        )) {
      xcb_free_gc(connection, graphics);
      xcb_render_free_picture(connection, picture);
      xcb_free_pixmap(connection, pixmap);
      return XCB_NONE;
    }

    xcb_free_gc(connection, graphics);
    xcb_render_free_picture(connection, picture);
    xcb_free_pixmap(connection, pixmap);
    return cursor;
  }

  bool loadCursorImages(CursorImage& themeImage, CursorImage& hotspotImage, CursorImage& customImage) {
    wlr_xcursor_theme* theme = wlr_xcursor_theme_load(std::getenv("XCURSOR_THEME"), kCursorSize);
    if (theme == nullptr) {
      std::println(stderr, "xwayland-cursor-client: failed to load the current Xcursor theme");
      return false;
    }
    wlr_xcursor* cursor = wlr_xcursor_theme_get_cursor(theme, "default");
    if (cursor == nullptr || cursor->image_count == 0 || cursor->images[0] == nullptr) {
      std::println(stderr, "xwayland-cursor-client: the current Xcursor theme has no default cursor");
      wlr_xcursor_theme_destroy(theme);
      return false;
    }
    const wlr_xcursor_image& image = *cursor->images[0];
    if (image.width == 0
        || image.height == 0
        || image.width > std::numeric_limits<uint16_t>::max()
        || image.height > std::numeric_limits<uint16_t>::max()
        || image.hotspot_x >= image.width
        || image.hotspot_y >= image.height) {
      std::println(stderr, "xwayland-cursor-client: the default cursor image has invalid geometry");
      wlr_xcursor_theme_destroy(theme);
      return false;
    }

    themeImage.width = static_cast<uint16_t>(image.width);
    themeImage.height = static_cast<uint16_t>(image.height);
    themeImage.hotspotX = static_cast<uint16_t>(image.hotspot_x);
    themeImage.hotspotY = static_cast<uint16_t>(image.hotspot_y);
    themeImage.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height));
    std::memcpy(themeImage.pixels.data(), image.buffer, themeImage.pixels.size() * sizeof(uint32_t));
    wlr_xcursor_theme_destroy(theme);

    hotspotImage = themeImage;
    if (hotspotImage.width > 1) {
      hotspotImage.hotspotX = hotspotImage.hotspotX == 0 ? 1 : 0;
    } else if (hotspotImage.height > 1) {
      hotspotImage.hotspotY = hotspotImage.hotspotY == 0 ? 1 : 0;
    } else {
      return false;
    }

    // Keep every matching shortcut, including geometry and hotspot, identical.
    // The opaque marker makes a content-only false promotion observable.
    customImage = themeImage;
    uint32_t& marker = customImage.pixels.back();
    marker = marker == 0xFFFF00FFU ? 0xFF00FFFFU : 0xFFFF00FFU;
    return true;
  }

  bool roundTrip(xcb_connection_t* connection) {
    const xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(connection);
    xcb_get_input_focus_reply_t* reply = xcb_get_input_focus_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
      return false;
    }
    std::free(reply);
    return true;
  }

  bool setCursor(State& state, xcb_cursor_t cursor, std::string_view name) {
    if (!checkRequest(
            state.connection,
            xcb_change_window_attributes_checked(state.connection, state.window, XCB_CW_CURSOR, &cursor),
            "changing the window cursor"
        )) {
      return false;
    }
    xcb_flush(state.connection);
    if (!roundTrip(state.connection)) {
      std::println(stderr, "xwayland-cursor-client: cursor round trip failed");
      return false;
    }
    std::println("cursor {}", name);
    return true;
  }

  void handleCommand(State& state, std::string_view command) {
    if (command == "theme") {
      state.running = setCursor(state, state.themeCursor, "theme");
    } else if (command == "hotspot") {
      state.running = setCursor(state, state.hotspotCursor, "hotspot");
    } else if (command == "custom") {
      state.running = setCursor(state, state.customCursor, "custom");
    } else if (command == "quit") {
      state.running = false;
    } else if (!command.empty()) {
      std::println(stderr, "xwayland-cursor-client: unknown command '{}'", command);
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
        std::println(stderr, "xwayland-cursor-client: stdin read failed: {}", std::strerror(errno));
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
    if (type != XCB_CLIENT_MESSAGE) {
      return;
    }
    const auto& message = reinterpret_cast<const xcb_client_message_event_t&>(event);
    if (message.type == state.atoms.wmProtocols && message.data.data32[0] == state.atoms.wmDeleteWindow) {
      state.running = false;
    }
  }

  bool createWindow(State& state, std::string_view title) {
    state.window = xcb_generate_id(state.connection);
    constexpr uint32_t eventMask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    const uint32_t values[] = {state.screen->black_pixel, eventMask, state.themeCursor};
    constexpr uint32_t valueMask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_CURSOR;
    if (!checkRequest(
            state.connection,
            xcb_create_window_checked(
                state.connection, XCB_COPY_FROM_PARENT, state.window, state.screen->root, 80, 80, 640, 480, 0,
                XCB_WINDOW_CLASS_INPUT_OUTPUT, state.screen->root_visual, valueMask, values
            ),
            "creating the test window"
        )) {
      return false;
    }
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, state.window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        static_cast<uint32_t>(title.size()), title.data()
    );
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, state.window, state.atoms.netWmName, state.atoms.utf8String, 8,
        static_cast<uint32_t>(title.size()), title.data()
    );
    const std::string wmClass = std::string(title) + '\0' + "UmbrielXwaylandCursor" + '\0';
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, state.window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
        static_cast<uint32_t>(wmClass.size()), wmClass.data()
    );
    xcb_change_property(
        state.connection, XCB_PROP_MODE_REPLACE, state.window, state.atoms.wmProtocols, XCB_ATOM_ATOM, 32, 1,
        &state.atoms.wmDeleteWindow
    );
    xcb_map_window(state.connection, state.window);
    xcb_flush(state.connection);
    return roundTrip(state.connection);
  }
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(stderr, "usage: xwayland-cursor-client TITLE");
    return EXIT_FAILURE;
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);

  int screenNumber = 0;
  State state;
  state.connection = xcb_connect(nullptr, &screenNumber);
  if (state.connection == nullptr || xcb_connection_has_error(state.connection) != 0) {
    std::println(stderr, "xwayland-cursor-client: failed to connect to DISPLAY");
    return EXIT_FAILURE;
  }
  const xcb_setup_t* setup = xcb_get_setup(state.connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  for (int index = 0; index < screenNumber && screens.rem != 0; ++index) {
    xcb_screen_next(&screens);
  }
  state.screen = screens.data;

  CursorImage themeImage;
  CursorImage hotspotImage;
  CursorImage customImage;
  const xcb_render_pictformat_t format = argbFormat(state.connection);
  if (state.screen == nullptr
      || format == XCB_NONE
      || !loadAtoms(state)
      || !loadCursorImages(themeImage, hotspotImage, customImage)) {
    std::println(stderr, "xwayland-cursor-client: failed to initialize");
    xcb_disconnect(state.connection);
    return EXIT_FAILURE;
  }
  state.themeCursor = createCursor(state.connection, state.screen, format, themeImage);
  state.hotspotCursor = createCursor(state.connection, state.screen, format, hotspotImage);
  state.customCursor = createCursor(state.connection, state.screen, format, customImage);
  if (state.themeCursor == XCB_NONE
      || state.hotspotCursor == XCB_NONE
      || state.customCursor == XCB_NONE
      || !createWindow(state, argv[1])) {
    std::println(stderr, "xwayland-cursor-client: failed to create cursor fixtures");
    xcb_disconnect(state.connection);
    return EXIT_FAILURE;
  }
  std::println(
      "ready window=0x{:x} cursor={}x{} hotspot={},{}", state.window, themeImage.width, themeImage.height,
      themeImage.hotspotX, themeImage.hotspotY
  );

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
      std::println(stderr, "xwayland-cursor-client: poll failed: {}", std::strerror(errno));
      state.running = false;
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

  xcb_destroy_window(state.connection, state.window);
  xcb_free_cursor(state.connection, state.themeCursor);
  xcb_free_cursor(state.connection, state.hotspotCursor);
  xcb_free_cursor(state.connection, state.customCursor);
  xcb_disconnect(state.connection);
  return state.running ? EXIT_FAILURE : EXIT_SUCCESS;
}
