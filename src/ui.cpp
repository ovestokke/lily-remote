#include "ui.h"

bool UiRect::contains(int16_t px, int16_t py) const {
  return px >= x && px < x + w && py >= y && py < y + h;
}

const char *uiPageName(UiPageId page) {
  switch (page) {
  case UiPageId::Home:
    return "home";
  case UiPageId::Media:
    return "media";
  case UiPageId::Lights:
    return "lights";
  case UiPageId::Room:
    return "room";
  case UiPageId::More:
    return "more";
  case UiPageId::SafeControl:
    return "safe_control";
  case UiPageId::TouchTest:
    return "touch_test";
  }
  return "unknown";
}
