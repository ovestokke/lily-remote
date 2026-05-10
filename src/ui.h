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

constexpr UiRect kTabTelia{24, 136, 123, 62};
constexpr UiRect kTabWiim{147, 136, 123, 62};
constexpr UiRect kTabTv{270, 136, 123, 62};
constexpr UiRect kTabLs60{393, 136, 123, 62};

// Telia Panel
constexpr UiRect kTeliaUp{151, 284, 127, 106};
constexpr UiRect kTeliaLeft{24, 390, 127, 106};
constexpr UiRect kTeliaOk{151, 390, 127, 106};
constexpr UiRect kTeliaRight{278, 390, 128, 106};
constexpr UiRect kTeliaDown{151, 496, 127, 106};
constexpr UiRect kTeliaBack{420, 284, 96, 152};
constexpr UiRect kTeliaHome{420, 450, 96, 152};
constexpr UiRect kTeliaRewind{24, 616, 164, 64};
constexpr UiRect kTeliaPlayPause{188, 616, 164, 64};
constexpr UiRect kTeliaFastForward{352, 616, 164, 64};
constexpr UiRect kTeliaPlex{24, 694, 164, 60};
constexpr UiRect kTeliaYouTube{188, 694, 164, 60};
constexpr UiRect kTeliaSpotify{352, 694, 164, 60};

// WiiM Panel
constexpr UiRect kWiimVolDown{24, 284, 164, 112};
constexpr UiRect kWiimMute{188, 284, 164, 112};
constexpr UiRect kWiimVolUp{352, 284, 164, 112};
constexpr UiRect kWiimHdmi{24, 410, 238, 92};
constexpr UiRect kWiimPhono{278, 410, 238, 92};
constexpr UiRect kWiimAux{24, 514, 238, 92};
constexpr UiRect kWiimWifi{278, 514, 238, 92};
constexpr UiRect kWiimPrev{24, 620, 164, 64};
constexpr UiRect kWiimPlay{188, 620, 164, 64};
constexpr UiRect kWiimNext{352, 620, 164, 64};

// TV Panel
constexpr UiRect kTvPowerOn{24, 284, 164, 78};
constexpr UiRect kTvPowerToggle{188, 284, 164, 78};
constexpr UiRect kTvPowerOff{352, 284, 164, 78};
constexpr UiRect kTvSourceTelia{24, 376, 238, 92};
constexpr UiRect kTvSourcePs5{278, 376, 238, 92};
constexpr UiRect kTvSourceHdmi4{24, 480, 238, 92};
constexpr UiRect kTvSourceLive{278, 480, 238, 92};

// LS60 Panel
constexpr UiRect kLs60Restore{24, 284, 492, 106};
constexpr UiRect kLs60Coax{24, 402, 164, 92};
constexpr UiRect kLs60Vol71{188, 402, 164, 92};
constexpr UiRect kLs60Analog{352, 402, 164, 92};
constexpr UiRect kLs60Optical{24, 506, 164, 92};
constexpr UiRect kLs60Tv{188, 506, 164, 92};
constexpr UiRect kLs60Bluetooth{352, 506, 164, 92};

const char *uiPageName(UiPageId page);
