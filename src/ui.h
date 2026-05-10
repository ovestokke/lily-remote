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

// Lights Panel
constexpr UiRect kSceneNormal{24, 134, 156, 82};
constexpr UiRect kSceneWatchTV{192, 134, 156, 82};
constexpr UiRect kSceneRelax{360, 134, 156, 82};

constexpr UiRect kZoneAll{24, 296, 492, 72};
constexpr UiRect kZoneAllOn{364, 296, 76, 72};
constexpr UiRect kZoneAllOff{440, 296, 76, 72};

constexpr UiRect kZoneHallway{24, 378, 492, 62};
constexpr UiRect kZoneHallwayOn{364, 378, 76, 62};
constexpr UiRect kZoneHallwayOff{440, 378, 76, 62};

constexpr UiRect kZoneKitchen{24, 450, 492, 62};
constexpr UiRect kZoneKitchenOn{364, 450, 76, 62};
constexpr UiRect kZoneKitchenOff{440, 450, 76, 62};

constexpr UiRect kZoneCorner{24, 522, 492, 62};
constexpr UiRect kZoneCornerOn{364, 522, 76, 62};
constexpr UiRect kZoneCornerOff{440, 522, 76, 62};

constexpr UiRect kZoneDining{24, 594, 492, 62};
constexpr UiRect kZoneDiningOn{364, 594, 76, 62};
constexpr UiRect kZoneDiningOff{440, 594, 76, 62};

constexpr UiRect kZoneTv{24, 666, 492, 62};
constexpr UiRect kZoneTvOn{364, 666, 76, 62};
constexpr UiRect kZoneTvOff{440, 666, 76, 62};

// Room Panel
constexpr UiRect kStatusActivity{24, 134, 492, 68};
constexpr UiRect kStatusTV{24, 212, 492, 68};
constexpr UiRect kStatusWiiM{24, 290, 492, 68};
constexpr UiRect kStatusLS60{24, 368, 492, 68};
constexpr UiRect kStatusLights{24, 446, 492, 68};

constexpr UiRect kRoomFixLS60{24, 530, 240, 82};
constexpr UiRect kRoomRefresh{276, 530, 240, 82};

// More Panel
constexpr UiRect kMoreAllOff{24, 128, 492, 82};
constexpr UiRect kMoreRefresh{24, 222, 240, 82};
constexpr UiRect kMoreFixLS60{276, 222, 240, 82};
constexpr UiRect kMoreWifi{24, 316, 240, 82};
constexpr UiRect kMoreAbout{276, 316, 240, 82};
constexpr UiRect kMoreSleep{24, 410, 240, 82};
constexpr UiRect kMoreSafe{276, 410, 240, 82};

const char *uiPageName(UiPageId page);
