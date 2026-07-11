#pragma once

#include <Arduino.h>

// Compact, original monochrome navigation artwork.  Bitmaps are kept in
// program memory so they work on the current OLED and can be reused by the
// SPI TFT renderer without a filesystem mount or a PNG decoder.
namespace NavigationIcons {

struct Bitmap {
  uint8_t width;
  uint8_t height;
  const uint8_t* data;
};

// Direction bits used by laneGlyph().  A lane can contain more than one
// arrow; enabledDirections marks the direction selected by the AMap code.
enum LaneDirection : uint8_t {
  LANE_STRAIGHT = 1 << 0,
  LANE_LEFT = 1 << 1,
  LANE_RIGHT = 1 << 2,
  LANE_U_LEFT = 1 << 3,
  LANE_U_RIGHT = 1 << 4,
  LANE_EXTEND = 1 << 5,
};

struct LaneGlyph {
  uint8_t directions;
  uint8_t enabledDirections;
};

// AMap turn NEW_ICON / ICON values. Unknown values intentionally fall back
// to a straight-ahead arrow, rather than leaving the display blank.
const Bitmap& turnBitmap(int icon);

// AMap CAMERA_TYPE values. The mapping groups values with the same traffic
// meaning (speed, red light, bus lane, ETC, pressure line, no-parking, etc.).
const Bitmap& cameraBitmap(int type);

// Maps AMap drive-way codes 0..48 and special codes 62, 85, 89 to a small
// vector description. This mirrors the original code catalogue rather than
// requiring dozens of large lane PNGs. A TFT can draw each bit as a path.
LaneGlyph laneGlyph(int laneCode);

}  // namespace NavigationIcons
