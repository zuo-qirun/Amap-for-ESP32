#include "NavigationIcons.h"

#include <pgmspace.h>

namespace NavigationIcons {
namespace {

// XBM-style, least-significant-bit-first rows. These are original 16x16
// direction glyphs, deliberately kept small for OTA-friendly firmware.
const uint8_t kTurnStraight[] PROGMEM = {
    0x00, 0x00, 0x80, 0x01, 0xc0, 0x03, 0xe0, 0x07,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
};
const uint8_t kTurnLeft[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x1c, 0x00,
    0xfe, 0x03, 0xff, 0x07, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
};
const uint8_t kTurnRight[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x38,
    0xc0, 0x7f, 0xe0, 0xff, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
};
const uint8_t kTurnSlightLeft[] PROGMEM = {
    0x00, 0x00, 0x0c, 0x00, 0x1e, 0x00, 0x3f, 0x00,
    0x30, 0x00, 0x20, 0x00, 0x60, 0x00, 0xc0, 0x00,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
};
const uint8_t kTurnSlightRight[] PROGMEM = {
    0x00, 0x00, 0x00, 0x30, 0x00, 0x78, 0x00, 0xfc,
    0x00, 0x0c, 0x00, 0x04, 0x00, 0x06, 0x00, 0x03,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
};
const uint8_t kTurnU[] PROGMEM = {
    0x00, 0x00, 0x1c, 0x00, 0x3e, 0x00, 0x63, 0x00,
    0x41, 0x00, 0xc1, 0x01, 0x81, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
};
const uint8_t kTurnKeepLeft[] PROGMEM = {
    0x00, 0x00, 0x0c, 0x00, 0x1e, 0x00, 0x3f, 0x00,
    0x30, 0x00, 0x20, 0x00, 0x60, 0x00, 0xc0, 0x00,
    0x80, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
};
const uint8_t kTurnKeepRight[] PROGMEM = {
    0x00, 0x00, 0x00, 0x30, 0x00, 0x78, 0x00, 0xfc,
    0x00, 0x0c, 0x00, 0x04, 0x00, 0x06, 0x00, 0x03,
    0x80, 0x01, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00,
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00,
};

// 12x12 camera symbols (two bytes per scanline).  They are visually distinct
// even on a 128x64 OLED and occupy only 144 bytes in flash in total.
const uint8_t kCameraGeneric[] PROGMEM = {
    0x00, 0x00, 0x3c, 0x00, 0x42, 0x00, 0x81, 0x00,
    0xa5, 0x00, 0x81, 0x00, 0x81, 0x00, 0xa5, 0x00,
    0x81, 0x00, 0x42, 0x00, 0x3c, 0x00, 0x00, 0x00,
};
const uint8_t kCameraSpeed[] PROGMEM = {
    0x3c, 0x00, 0x42, 0x00, 0x99, 0x00, 0xa1, 0x00,
    0xa1, 0x00, 0xa1, 0x00, 0xa1, 0x00, 0xa1, 0x00,
    0xa1, 0x00, 0x99, 0x00, 0x42, 0x00, 0x3c, 0x00,
};
const uint8_t kCameraLight[] PROGMEM = {
    0x3c, 0x00, 0x42, 0x00, 0x81, 0x00, 0x99, 0x00,
    0x81, 0x00, 0x99, 0x00, 0x81, 0x00, 0x99, 0x00,
    0x81, 0x00, 0x42, 0x00, 0x3c, 0x00, 0x00, 0x00,
};
const uint8_t kCameraBus[] PROGMEM = {
    0x7e, 0x00, 0x81, 0x00, 0xbd, 0x00, 0x81, 0x00,
    0x81, 0x00, 0xbd, 0x00, 0x81, 0x00, 0x81, 0x00,
    0x99, 0x00, 0x42, 0x00, 0x24, 0x00, 0x00, 0x00,
};
const uint8_t kCameraEtc[] PROGMEM = {
    0x7f, 0x00, 0x41, 0x00, 0x5d, 0x00, 0x41, 0x00,
    0x5d, 0x00, 0x41, 0x00, 0x5d, 0x00, 0x41, 0x00,
    0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
const uint8_t kCameraWarning[] PROGMEM = {
    0x10, 0x00, 0x38, 0x00, 0x7c, 0x00, 0xfe, 0x00,
    0x92, 0x00, 0x92, 0x00, 0x92, 0x00, 0xfe, 0x00,
    0x7c, 0x00, 0x38, 0x00, 0x10, 0x00, 0x00, 0x00,
};

const Bitmap kStraight = {16, 16, kTurnStraight};
const Bitmap kLeft = {16, 16, kTurnLeft};
const Bitmap kRight = {16, 16, kTurnRight};
const Bitmap kSlightLeft = {16, 16, kTurnSlightLeft};
const Bitmap kSlightRight = {16, 16, kTurnSlightRight};
const Bitmap kU = {16, 16, kTurnU};
const Bitmap kKeepLeft = {16, 16, kTurnKeepLeft};
const Bitmap kKeepRight = {16, 16, kTurnKeepRight};
const Bitmap kGenericCamera = {12, 12, kCameraGeneric};
const Bitmap kSpeedCamera = {12, 12, kCameraSpeed};
const Bitmap kLightCamera = {12, 12, kCameraLight};
const Bitmap kBusCamera = {12, 12, kCameraBus};
const Bitmap kEtcCamera = {12, 12, kCameraEtc};
const Bitmap kWarningCamera = {12, 12, kCameraWarning};

LaneGlyph lane(uint8_t directions, uint8_t enabled) {
  return {directions, enabled};
}

}  // namespace

const Bitmap& turnBitmap(int icon) {
  switch (icon) {
    case 2: return kLeft;
    case 3: return kRight;
    case 4:
    case 6: return kSlightLeft;
    case 5:
    case 7: return kSlightRight;
    case 8: return kU;
    case 18: return kKeepLeft;
    case 19: return kKeepRight;
    default: return kStraight;
  }
}

const Bitmap& cameraBitmap(int type) {
  switch (type) {
    case 0: return kSpeedCamera;
    case 11: return kEtcCamera;
    case 1:
    case 14:
    case 23: return kGenericCamera;
    case 2:
    case 15: return kLightCamera;
    case 4:
    case 16: return kBusCamera;
    case 12:
    case 21:
    case 24: return kWarningCamera;
    default: return kGenericCamera;
  }
}

LaneGlyph laneGlyph(int code) {
  switch (code) {
    case 0: return lane(LANE_STRAIGHT, 0);
    case 1: return lane(LANE_LEFT, 0);
    case 2: return lane(LANE_STRAIGHT | LANE_LEFT, 0);
    case 3: return lane(LANE_RIGHT, 0);
    case 4: return lane(LANE_STRAIGHT | LANE_RIGHT, 0);
    case 5: return lane(LANE_U_LEFT, 0);
    case 6: return lane(LANE_LEFT | LANE_RIGHT, 0);
    case 7: return lane(LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT, 0);
    case 8: return lane(LANE_U_RIGHT, 0);
    case 9: return lane(LANE_U_LEFT | LANE_STRAIGHT, 0);
    case 10: return lane(LANE_STRAIGHT | LANE_U_RIGHT, 0);
    case 11: return lane(LANE_U_LEFT | LANE_LEFT, 0);
    case 12: return lane(LANE_RIGHT | LANE_U_RIGHT, 0);
    case 13:
    case 14: return lane(LANE_EXTEND | LANE_STRAIGHT, 0);
    case 15: return lane(LANE_STRAIGHT, LANE_STRAIGHT);
    case 16: return lane(LANE_LEFT, LANE_LEFT);
    case 17: return lane(LANE_LEFT | LANE_STRAIGHT, LANE_LEFT | LANE_STRAIGHT);
    case 18: return lane(LANE_RIGHT, LANE_RIGHT);
    case 19: return lane(LANE_STRAIGHT | LANE_RIGHT, LANE_STRAIGHT | LANE_RIGHT);
    case 20: return lane(LANE_U_LEFT, LANE_U_LEFT);
    case 21: return lane(LANE_LEFT | LANE_RIGHT, LANE_LEFT | LANE_RIGHT);
    case 22: return lane(LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT,
                         LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT);
    case 23: return lane(LANE_U_RIGHT, LANE_U_RIGHT);
    case 24: return lane(LANE_U_LEFT | LANE_STRAIGHT, LANE_U_LEFT | LANE_STRAIGHT);
    case 25: return lane(LANE_STRAIGHT | LANE_U_RIGHT, LANE_STRAIGHT | LANE_U_RIGHT);
    case 26: return lane(LANE_U_LEFT | LANE_LEFT, LANE_U_LEFT | LANE_LEFT);
    case 27: return lane(LANE_RIGHT | LANE_U_RIGHT, LANE_RIGHT | LANE_U_RIGHT);
    case 28:
    case 29: return lane(LANE_EXTEND | LANE_STRAIGHT, LANE_EXTEND | LANE_STRAIGHT);
    case 30: return lane(LANE_STRAIGHT | LANE_LEFT, LANE_STRAIGHT);
    case 31: return lane(LANE_STRAIGHT | LANE_LEFT, LANE_LEFT);
    case 32: return lane(LANE_STRAIGHT | LANE_RIGHT, LANE_STRAIGHT);
    case 33: return lane(LANE_STRAIGHT | LANE_RIGHT, LANE_RIGHT);
    case 34: return lane(LANE_LEFT | LANE_RIGHT, LANE_LEFT);
    case 35: return lane(LANE_LEFT | LANE_RIGHT, LANE_RIGHT);
    case 36: return lane(LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT, LANE_STRAIGHT);
    case 37: return lane(LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT, LANE_LEFT);
    case 38: return lane(LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT, LANE_RIGHT);
    case 39: return lane(LANE_U_LEFT | LANE_STRAIGHT, LANE_STRAIGHT);
    case 40: return lane(LANE_U_LEFT | LANE_STRAIGHT, LANE_U_LEFT);
    case 41: return lane(LANE_STRAIGHT | LANE_U_RIGHT, LANE_STRAIGHT);
    case 42: return lane(LANE_STRAIGHT | LANE_U_RIGHT, LANE_U_RIGHT);
    case 43: return lane(LANE_LEFT | LANE_U_LEFT, LANE_LEFT);
    case 44:
    case 48: return lane(LANE_LEFT | LANE_U_LEFT, LANE_U_LEFT);
    case 45: return lane(LANE_RIGHT | LANE_U_RIGHT, LANE_RIGHT);
    case 46: return lane(LANE_RIGHT | LANE_U_RIGHT, LANE_U_RIGHT);
    case 47: return lane(LANE_EXTEND | LANE_LEFT | LANE_U_RIGHT, LANE_U_RIGHT);
    case 62: return lane(LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT,
                         LANE_LEFT | LANE_STRAIGHT | LANE_RIGHT);
    case 85: return lane(LANE_EXTEND | LANE_STRAIGHT, LANE_STRAIGHT);
    case 89: return lane(0, 0);
    default: return lane(LANE_STRAIGHT, LANE_STRAIGHT);
  }
}

}  // namespace NavigationIcons
