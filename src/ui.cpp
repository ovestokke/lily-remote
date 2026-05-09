#include "ui.h"

bool UiRect::contains(int16_t px, int16_t py) const {
  return px >= x && px < x + w && py >= y && py < y + h;
}

const char *uiPageName(UiPageId page) {
  switch (page) {
  case UiPageId::SafeControl:
    return "safe_control";
  case UiPageId::Activities:
    return "activities";
  case UiPageId::Media:
    return "media";
  case UiPageId::Lights:
    return "lights";
  case UiPageId::Info:
    return "info";
  case UiPageId::Status:
    return "status";
  case UiPageId::TouchTest:
    return "touch_test";
  }
  return "unknown";
}
