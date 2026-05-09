#pragma once

#include <Arduino.h>

struct UiRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;

  constexpr UiRect() : x(0), y(0), w(0), h(0) {}
  constexpr UiRect(int16_t x_, int16_t y_, int16_t w_, int16_t h_)
      : x(x_), y(y_), w(w_), h(h_) {}

  bool contains(int16_t px, int16_t py) const;
};

enum class UiPageId : uint8_t {
  Home,
  Media,
  Lights,
  Room,
  More,
  SafeControl,
  TouchTest,
};

constexpr int16_t kUiScreenWidth = 540;
constexpr int16_t kUiScreenHeight = 960;
constexpr UiRect kSafeControlToggleButton{64, 520, 412, 150};

constexpr UiRect kActivityWatchTvButton{24, 238, 238, 126};
constexpr UiRect kActivityPs5Button{278, 238, 238, 126};
constexpr UiRect kActivityMusicButton{24, 378, 238, 126};
constexpr UiRect kActivityRecordsButton{278, 378, 238, 126};
constexpr UiRect kMediaOffButton{420, 40, 96, 38};
constexpr UiRect kOnlinePillRect{312, 40, 98, 38};

constexpr UiRect kQuickVolDown{24, 746, 94, 76};
constexpr UiRect kQuickPrev{118, 746, 94, 76};
constexpr UiRect kQuickPlay{212, 746, 116, 76};
constexpr UiRect kQuickNext{328, 746, 94, 76};
constexpr UiRect kQuickVolUp{422, 746, 94, 76};

constexpr UiRect kChipTV{24, 686, 157, 50};
constexpr UiRect kChipWiiM{191, 686, 158, 50};
constexpr UiRect kChipKEF{359, 686, 157, 50};

constexpr UiRect kNavHome{24, 862, 98, 58};
constexpr UiRect kNavMedia{122, 862, 98, 58};
constexpr UiRect kNavLights{220, 862, 100, 58};
constexpr UiRect kNavRoom{320, 862, 98, 58};
constexpr UiRect kNavMore{418, 862, 98, 58};

constexpr UiRect kDeviceTeliaTab{40, 224, 115, 62};
constexpr UiRect kDeviceWiimTab{155, 224, 115, 62};
constexpr UiRect kDeviceTvTab{270, 224, 115, 62};
constexpr UiRect kDeviceLs60Tab{385, 224, 115, 62};

constexpr UiRect kNavBackButton{68, 328, 199, 54};
constexpr UiRect kNavHomeButton{273, 328, 199, 54};
constexpr UiRect kNavUpButton{214, 410, 112, 112};
constexpr UiRect kNavLeftButton{102, 522, 112, 112};
constexpr UiRect kNavOkButton{214, 522, 112, 112};
constexpr UiRect kNavRightButton{326, 522, 112, 112};
constexpr UiRect kNavDownButton{214, 634, 112, 112};

constexpr UiRect kDeviceWideButton{68, 328, 404, 112};
constexpr UiRect kDeviceLeftTopButton{68, 456, 194, 112};
constexpr UiRect kDeviceRightTopButton{278, 456, 194, 112};
constexpr UiRect kDeviceLeftBottomButton{68, 584, 194, 112};
constexpr UiRect kDeviceRightBottomButton{278, 584, 194, 112};
constexpr UiRect kDeviceHintBox{68, 714, 404, 50};

const char *uiPageName(UiPageId page);
