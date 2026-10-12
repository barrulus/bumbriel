#include "input/xcursor_matcher.h"

#include "check.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <wayland-util.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xcursor.h>
}

using umbriel::matchXcursorManagerImage;
using umbriel::matchXcursorThemeImage;
using umbriel::XcursorImageView;

namespace {

  template <size_t Size>
  XcursorImageView view(
      const std::array<uint32_t, Size>& pixels, uint32_t width, uint32_t height, uint32_t hotspotX = 0,
      uint32_t hotspotY = 0
  ) {
    return XcursorImageView{
        .width = width,
        .height = height,
        .hotspotX = hotspotX,
        .hotspotY = hotspotY,
        .pixels = reinterpret_cast<const uint8_t*>(pixels.data()),
        .stride = static_cast<size_t>(width) * sizeof(uint32_t),
    };
  }

  struct ThemeCursor {
    ThemeCursor(const char* name, std::initializer_list<wlr_xcursor_image*> frames) : frameStorage(frames) {
      cursor.image_count = static_cast<unsigned int>(frameStorage.size());
      cursor.images = frameStorage.data();
      cursor.name = const_cast<char*>(name);
    }

    std::vector<wlr_xcursor_image*> frameStorage;
    wlr_xcursor cursor{};
  };

  struct Theme {
    Theme(std::initializer_list<ThemeCursor*> cursors) {
      for (ThemeCursor* cursor : cursors) {
        cursorStorage.push_back(&cursor->cursor);
      }
      theme.cursor_count = static_cast<unsigned int>(cursorStorage.size());
      theme.cursors = cursorStorage.data();
    }

    std::vector<wlr_xcursor*> cursorStorage;
    wlr_xcursor_theme theme{};
  };

  template <size_t Size>
  wlr_xcursor_image themeImage(
      std::array<uint32_t, Size>& pixels, uint32_t width, uint32_t height, uint32_t hotspotX = 0, uint32_t hotspotY = 0
  ) {
    wlr_xcursor_image image{};
    image.width = width;
    image.height = height;
    image.hotspot_x = hotspotX;
    image.hotspot_y = hotspotY;
    image.buffer = reinterpret_cast<uint8_t*>(pixels.data());
    return image;
  }

} // namespace

UMBRIEL_TEST(comparesRowsWithoutReadingStridePadding) {
  std::array<uint32_t, 4> packed{0xff102030U, 0xff405060U, 0xff708090U, 0xffa0b0c0U};
  std::array<uint32_t, 6> padded{
      0xff102030U, 0xff405060U, 0xdeadbeefU, 0xff708090U, 0xffa0b0c0U, 0xcafebabeU,
  };
  XcursorImageView paddedView = view(padded, 2, 2);
  paddedView.stride = 3 * sizeof(uint32_t);

  CHECK(umbriel::xcursorImagesEqual(view(packed, 2, 2), paddedView));
}

UMBRIEL_TEST(normalizesRgbOnlyWhenAlphaIsFullyTransparent) {
  std::array<uint32_t, 2> first{0x00112233U, 0x01445566U};
  std::array<uint32_t, 2> transparentDifference{0x00abcdefU, 0x01445566U};
  std::array<uint32_t, 2> partialDifference{0x00112233U, 0x01445567U};

  CHECK(umbriel::xcursorImagesEqual(view(first, 2, 1), view(transparentDifference, 2, 1)));
  CHECK(!umbriel::xcursorImagesEqual(view(first, 2, 1), view(partialDifference, 2, 1)));
}

UMBRIEL_TEST(requiresDimensionsAndHotspotToMatch) {
  std::array<uint32_t, 4> pixels{0xff000001U, 0xff000002U, 0xff000003U, 0xff000004U};

  CHECK(!umbriel::xcursorImagesEqual(view(pixels, 2, 2), view(pixels, 1, 4)));
  CHECK(!umbriel::xcursorImagesEqual(view(pixels, 2, 2, 0, 0), view(pixels, 2, 2, 1, 0)));
}

UMBRIEL_TEST(rejectsMissingPixelsAndShortRows) {
  std::array<uint32_t, 1> pixels{0xff123456U};
  XcursorImageView missing = view(pixels, 1, 1);
  missing.pixels = nullptr;
  XcursorImageView shortRow = view(pixels, 1, 1);
  shortRow.stride = sizeof(uint32_t) - 1;

  CHECK(!umbriel::xcursorImagesEqual(view(pixels, 1, 1), missing));
  CHECK(!umbriel::xcursorImagesEqual(view(pixels, 1, 1), shortRow));
}

UMBRIEL_TEST(preferredSemanticNameWinsIdenticalAliases) {
  std::array<uint32_t, 1> pixels{0xff123456U};
  wlr_xcursor_image image = themeImage(pixels, 1, 1);
  ThemeCursor first("first", {&image});
  ThemeCursor preferred("preferred", {&image});
  Theme theme({&first, &preferred});

  CHECK_EQ(matchXcursorThemeImage(&theme.theme, view(pixels, 1, 1)), std::optional<std::string>{"first"});
  CHECK_EQ(
      matchXcursorThemeImage(&theme.theme, view(pixels, 1, 1), "preferred"), std::optional<std::string>{"preferred"}
  );
}

UMBRIEL_TEST(matchesAnyAnimationFrame) {
  std::array<uint32_t, 1> firstPixels{0xff000001U};
  std::array<uint32_t, 1> secondPixels{0xff000002U};
  wlr_xcursor_image firstImage = themeImage(firstPixels, 1, 1);
  wlr_xcursor_image secondImage = themeImage(secondPixels, 1, 1);
  ThemeCursor animated("animated", {&firstImage, &secondImage});
  Theme theme({&animated});

  CHECK_EQ(matchXcursorThemeImage(&theme.theme, view(secondPixels, 1, 1)), std::optional<std::string>{"animated"});
}

UMBRIEL_TEST(rejectsAnOpaquePixelDifference) {
  std::array<uint32_t, 1> themePixels{0xff000001U};
  std::array<uint32_t, 1> candidatePixels{0xff000002U};
  wlr_xcursor_image image = themeImage(themePixels, 1, 1);
  ThemeCursor cursor("pointer", {&image});
  Theme theme({&cursor});

  CHECK(!matchXcursorThemeImage(&theme.theme, view(candidatePixels, 1, 1)).has_value());
}

UMBRIEL_TEST(managerSearchesItsScaleOneTheme) {
  std::array<uint32_t, 1> basePixels{0xff123456U};
  std::array<uint32_t, 1> scaledPixels{0xff123456U};
  wlr_xcursor_image baseImage = themeImage(basePixels, 1, 1);
  wlr_xcursor_image scaledImage = themeImage(scaledPixels, 1, 1);
  ThemeCursor baseCursor("base-pointer", {&baseImage});
  ThemeCursor scaledCursor("scaled-pointer", {&scaledImage});
  Theme baseTheme({&baseCursor});
  Theme scaledThemeData({&scaledCursor});

  wlr_xcursor_manager manager{};
  wl_list_init(&manager.scaled_themes);
  wlr_xcursor_manager_theme baseThemeEntry{};
  baseThemeEntry.scale = 1.0F;
  baseThemeEntry.theme = &baseTheme.theme;
  wlr_xcursor_manager_theme scaledTheme{};
  scaledTheme.scale = 2.0F;
  scaledTheme.theme = &scaledThemeData.theme;
  wl_list_insert(&manager.scaled_themes, &baseThemeEntry.link);
  wl_list_insert(&manager.scaled_themes, &scaledTheme.link);

  CHECK_EQ(matchXcursorManagerImage(&manager, view(basePixels, 1, 1)), std::optional<std::string>{"base-pointer"});
}

int main() { return RUN_TESTS(); }
