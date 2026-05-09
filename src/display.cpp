#include "display.h"

#include "ui.h"
#include "icons.h"

#include <Arduino.h>
#include <FastEPD.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#ifndef REMOTE_ENABLE_GRAYSCALE_TEST
#define REMOTE_ENABLE_GRAYSCALE_TEST 0
#endif

namespace {
constexpr int32_t kNativeDisplayWidth = 960;
constexpr int32_t kNativeDisplayHeight = 540;
constexpr int32_t kPanelBusSpeedHz = 26666666;

// Final target orientation: portrait, USB/charging port down.
// If the next visual check shows top/bottom swapped, change this to 270.
constexpr int32_t kPortraitRotation = 90;

FASTEPD g_epaper;
bool g_displayReady = false;

String truncateForScreen(const String &value, size_t maxChars) {
  if (value.length() <= maxChars) {
    return value;
  }
  if (maxChars <= 3) {
    return value.substring(0, maxChars);
  }
  return value.substring(0, maxChars - 3) + "...";
}

void setTextBlack() {
  g_epaper.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);
}

void setTextWhite() {
  g_epaper.setTextColor(BBEP_WHITE, BBEP_TRANSPARENT);
}

void drawText(const String &text, int32_t x, int32_t y) {
  g_epaper.drawString(text.c_str(), x, y);
}

void drawText16(const String &text, int32_t x, int32_t y) {
  g_epaper.setFont(FONT_16x16);
  drawText(text, x, y);
  g_epaper.setFont(FONT_12x16);
}

void drawText8(const String &text, int32_t x, int32_t y) {
  g_epaper.setFont(FONT_8x8);
  drawText(text, x, y);
  g_epaper.setFont(FONT_12x16);
}

void drawInvertedText(const String &text, int32_t x, int32_t y) {
  setTextWhite();
  drawText(text, x, y);
  setTextBlack();
}

void drawMdiIcon(const uint8_t *bmp, int32_t x, int32_t y, uint8_t color = BBEP_BLACK) {
  g_epaper.loadBMP(bmp, x, y, color, BBEP_TRANSPARENT);
}

void drawLabelPill(int32_t x, int32_t y, int32_t w, int32_t h, const String &label, bool inverted = false) {
  g_epaper.drawRect(x, y, w, h, BBEP_BLACK);
  if (inverted) {
    g_epaper.fillRect(x, y, w, h, BBEP_BLACK);
    setTextWhite();
    drawText8(label, x + 10, y + 14);
    setTextBlack();
  } else {
    drawText8(label, x + 10, y + 14);
  }
}

void drawDiagonalTexture(int32_t x, int32_t y, int32_t w, int32_t h, int32_t spacing = 12) {
  for (int32_t i = -h; i < w; i += spacing) {
    g_epaper.drawLine(x + i, y + h, x + i + h, y, BBEP_BLACK);
  }
}

void drawWrappedText(const String &text, int32_t x, int32_t y, size_t charsPerLine, uint8_t maxLines) {
  String remaining = text;
  for (uint8_t line = 0; line < maxLines && remaining.length() > 0; ++line) {
    String chunk;
    if (remaining.length() <= charsPerLine) {
      chunk = remaining;
      remaining = "";
    } else {
      int split = remaining.lastIndexOf(' ', charsPerLine);
      if (split < 12) {
        split = charsPerLine;
      }
      chunk = remaining.substring(0, split);
      remaining = remaining.substring(split);
      remaining.trim();
    }

    if (line == maxLines - 1 && remaining.length() > 0) {
      chunk = truncateForScreen(chunk, charsPerLine - 3) + "...";
    }
    drawText(chunk, x, y + line * 20);
  }
}

void drawStatusBox(int32_t x, int32_t y, int32_t w, int32_t h, const String &label, bool ok) {
  g_epaper.drawRect(x, y, w, h, BBEP_BLACK);
  g_epaper.fillRect(x, y, w, 22, BBEP_BLACK);
  drawInvertedText(label, x + 10, y + 5);
  if (ok) {
    g_epaper.fillRect(x + 10, y + 38, 22, 22, BBEP_BLACK);
    drawText("OK", x + 42, y + 42);
  } else {
    drawDiagonalTexture(x + 8, y + 32, w - 16, h - 40, 10);
    g_epaper.fillRect(x + 10, y + 38, 22, 22, BBEP_BLACK);
    drawInvertedText("!", x + 17, y + 42);
    drawText("FAIL", x + 42, y + 42);
  }
}

void drawButton(const UiRect &rect, const String &title, const String &subtitle) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 6, rect.y + 6, rect.w - 12, rect.h - 12, BBEP_BLACK);
  g_epaper.fillRect(rect.x, rect.y, rect.w, 44, BBEP_BLACK);
  drawInvertedText("HA SAFE ACTION", rect.x + 24, rect.y + 14);
  drawText16(title, rect.x + 28, rect.y + 66);
  drawText(subtitle, rect.x + 28, rect.y + 106);
  g_epaper.fillRect(rect.x + rect.w - 42, rect.y + 44, 16, rect.h - 62, BBEP_BLACK);
}

void drawHLine(int32_t x, int32_t y, int32_t w) {
  g_epaper.drawLine(x, y, x + w, y, BBEP_BLACK);
}

void drawPageHeader(const String &title, const String &subtitle) {
  g_epaper.fillRect(20, 20, 16, 86, BBEP_BLACK);
  drawText8("LILY REMOTE", 54, 32);
  drawText16(title, 54, 56);
  if (subtitle.length() > 0) {
    drawWrappedText(subtitle, 54, 86, 40, 1);
  }
  g_epaper.fillRect(kUiScreenWidth - 126, 32, 86, 28, BBEP_BLACK);
  drawInvertedText("PORTRAIT", kUiScreenWidth - 116, 39);
  drawHLine(20, 120, kUiScreenWidth - 40);
}

void drawStatusText(const String &label, const String &value, int32_t x, int32_t y) {
  drawText(label, x, y);
  drawWrappedText(value, x, y + 26, 52, 3);
}

void drawSegmentNavItem(const UiRect &rect,
                        const String &label,
                        UiPageId itemPage,
                        UiPageId currentPage,
                        const uint8_t *iconBmp) {
  const bool active = itemPage == currentPage;
  if (active) {
    g_epaper.fillRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
    drawMdiIcon(iconBmp, rect.x + 35, rect.y + 6, BBEP_WHITE);
    drawInvertedText(label, rect.x + 18, rect.y + 36);
  } else {
    drawMdiIcon(iconBmp, rect.x + 35, rect.y + 6, BBEP_BLACK);
    drawText(label, rect.x + 18, rect.y + 36);
  }
}

void drawFooterNav(UiPageId currentPage, const String &hint) {
  (void)hint;
  drawHLine(20, kUiScreenHeight - 112, kUiScreenWidth - 40);
  g_epaper.drawRect(40, 862, 460, 58, BBEP_BLACK);
  g_epaper.drawLine(132, 862, 132, 920, BBEP_BLACK);
  g_epaper.drawLine(224, 862, 224, 920, BBEP_BLACK);
  g_epaper.drawLine(316, 862, 316, 920, BBEP_BLACK);
  g_epaper.drawLine(408, 862, 408, 920, BBEP_BLACK);

  drawSegmentNavItem(kBottomNavHomeButton, "HOME", UiPageId::Activities, currentPage, kMdiHome22Bmp);
  drawSegmentNavItem(kBottomNavMediaButton, "MEDIA", UiPageId::Media, currentPage, kMdiPlay22Bmp);
  drawSegmentNavItem(kBottomNavLightsButton, "LIGHT", UiPageId::Lights, currentPage, kMdiLight22Bmp);
  drawSegmentNavItem(kBottomNavInfoButton, "INFO", UiPageId::Info, currentPage, kMdiInfo22Bmp);
  drawSegmentNavItem(kBottomNavMoreButton, "MORE", UiPageId::Status, currentPage, kMdiDots22Bmp);

}

void drawShellCard(int32_t x, int32_t y, int32_t w, int32_t h, const String &title, const String &body) {
  g_epaper.drawRect(x, y, w, h, BBEP_BLACK);
  g_epaper.fillRect(x, y, 14, h, BBEP_BLACK);
  g_epaper.fillRect(x + 14, y, w - 14, 28, BBEP_BLACK);
  drawInvertedText(title, x + 28, y + 7);
  drawWrappedText(body, x + 28, y + 52, 42, 5);
}

void drawActivityButton(const UiRect &rect, const String &title, const uint8_t *iconBmp) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  g_epaper.fillRect(rect.x, rect.y, 12, rect.h, BBEP_BLACK);
  drawMdiIcon(iconBmp, rect.x + 24, rect.y + 18, BBEP_BLACK);
  drawText16(title, rect.x + 24, rect.y + 88);
}

void drawQuickStatusChip(int32_t x, int32_t y, const String &top, const String &bottom) {
  g_epaper.drawRect(x, y, 146, 50, BBEP_BLACK);
  drawText(top, x + 52, y + 9);
  drawText(bottom, x + 40, y + 27);
}

const char *deviceTargetName(RemoteDeviceTarget target) {
  switch (target) {
  case RemoteDeviceTarget::Telia:
    return "Telia Box";
  case RemoteDeviceTarget::Wiim:
    return "WiiM";
  case RemoteDeviceTarget::Tv:
    return "TV";
  case RemoteDeviceTarget::Ls60:
    return "LS60";
  }
  return "Unknown";
}

void drawKisssTopBar(bool online) {
  g_epaper.fillRect(20, 20, 16, 58, BBEP_BLACK);
  drawText8("LILY REMOTE", 54, 42);
  drawText8("LIVING ROOM", 54, 60);
  drawLabelPill(334, 40, 78, 38, online ? "ONLINE" : "OFFLINE");
  g_epaper.drawRect(kMediaOffButton.x, kMediaOffButton.y, kMediaOffButton.w, kMediaOffButton.h, BBEP_BLACK);
  g_epaper.fillRect(kMediaOffButton.x, kMediaOffButton.y, kMediaOffButton.w, kMediaOffButton.h, BBEP_BLACK);
  drawMdiIcon(kMdiPower18Bmp, kMediaOffButton.x + 9, kMediaOffButton.y + 10, BBEP_WHITE);
  drawInvertedText("OFF", kMediaOffButton.x + 32, kMediaOffButton.y + 11);
  drawHLine(20, 98, kUiScreenWidth - 40);
}

void drawDeviceTargetBox(RemoteDeviceTarget target) {
  g_epaper.drawRect(368, 116, 132, 72, BBEP_BLACK);
  g_epaper.fillRect(368, 116, 132, 25, BBEP_BLACK);
  drawInvertedText("TARGET", 378, 122);
  drawText(deviceTargetName(target), 388, 162);
}

void drawDeviceTab(const UiRect &rect,
                   const String &label,
                   RemoteDeviceTarget tabTarget,
                   RemoteDeviceTarget currentTarget,
                   const uint8_t *iconBmp) {
  const bool active = tabTarget == currentTarget;
  if (active) {
    g_epaper.fillRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
    drawMdiIcon(iconBmp, rect.x + 46, rect.y + 7, BBEP_WHITE);
    drawInvertedText(label, rect.x + 30, rect.y + 37);
  } else {
    drawMdiIcon(iconBmp, rect.x + 46, rect.y + 7, BBEP_BLACK);
    drawText(label, rect.x + 30, rect.y + 37);
  }
}

void drawDeviceTabs(RemoteDeviceTarget target) {
  g_epaper.drawRect(40, 224, 460, 62, BBEP_BLACK);
  g_epaper.drawLine(155, 224, 155, 286, BBEP_BLACK);
  g_epaper.drawLine(270, 224, 270, 286, BBEP_BLACK);
  g_epaper.drawLine(385, 224, 385, 286, BBEP_BLACK);
  drawDeviceTab(kDeviceTeliaTab, "TELIA", RemoteDeviceTarget::Telia, target, kMdiBox23Bmp);
  drawDeviceTab(kDeviceWiimTab, "WIIM", RemoteDeviceTarget::Wiim, target, kMdiMusic23Bmp);
  drawDeviceTab(kDeviceTvTab, "TV", RemoteDeviceTarget::Tv, target, kMdiTv23Bmp);
  drawDeviceTab(kDeviceLs60Tab, "LS60", RemoteDeviceTarget::Ls60, target, kMdiSpeaker23Bmp);
}

void drawPanelFrame() {
  g_epaper.drawRect(40, 304, 460, 480, BBEP_BLACK);
  g_epaper.drawRect(41, 305, 458, 478, BBEP_BLACK);
  g_epaper.fillRect(40, 304, 12, 480, BBEP_BLACK);
}

void drawTopActionButton(const UiRect &rect, const String &label, const uint8_t *iconBmp) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  drawMdiIcon(iconBmp, rect.x + 14, rect.y + 13, BBEP_BLACK);
  drawText(label, rect.x + 54, rect.y + 20);
}

void drawDpadKey(const UiRect &rect, const String &label, const uint8_t *iconBmp) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  drawMdiIcon(iconBmp, rect.x + 35, rect.y + 22, BBEP_BLACK);
  drawText8(label, rect.x + 42, rect.y + 80);
}

void drawOkKey(const UiRect &rect) {
  g_epaper.fillRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawCircle(rect.x + 56, rect.y + 56, 28, BBEP_WHITE);
  g_epaper.drawCircle(rect.x + 56, rect.y + 56, 29, BBEP_WHITE);
  drawInvertedText("OK", rect.x + 47, rect.y + 50);
}

void drawDeviceGridButton(const UiRect &rect, const String &label, const uint8_t *iconBmp, bool inverted = false) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  if (inverted) {
    g_epaper.fillRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
    drawMdiIcon(iconBmp, rect.x + 24, rect.y + 32, BBEP_WHITE);
    setTextWhite();
    drawText16(label, rect.x + 94, rect.y + 48);
    setTextBlack();
    return;
  }
  drawMdiIcon(iconBmp, rect.x + rect.w / 2 - 24, rect.y + 20, BBEP_BLACK);
  drawText16(label, rect.x + 24, rect.y + 78);
}

void drawHintBox(const String &hint) {
  g_epaper.drawRect(kDeviceHintBox.x, kDeviceHintBox.y, kDeviceHintBox.w, kDeviceHintBox.h, BBEP_BLACK);
  drawText8(hint, kDeviceHintBox.x + 14, kDeviceHintBox.y + 20);
}

void renderTeliaControls() {
  drawTopActionButton(kNavBackButton, "Back", kMdiBack28Bmp);
  drawTopActionButton(kNavHomeButton, "Home", kMdiHome28Bmp);
  drawDpadKey(kNavUpButton, "UP", kMdiUp42Bmp);
  drawDpadKey(kNavLeftButton, "LEFT", kMdiLeft42Bmp);
  drawOkKey(kNavOkButton);
  drawDpadKey(kNavRightButton, "RIGHT", kMdiRight42Bmp);
  drawDpadKey(kNavDownButton, "DOWN", kMdiDown42Bmp);
}

void renderWiimControls() {
  drawDeviceGridButton(kDeviceWideButton, "Volume +", kMdiVolume48Bmp, true);
  drawDeviceGridButton(kDeviceLeftTopButton, "HDMI", kMdiHdmi48Bmp);
  drawDeviceGridButton(kDeviceRightTopButton, "Phono", kMdiRecord48Bmp);
  drawDeviceGridButton(kDeviceLeftBottomButton, "Mute", kMdiMute48Bmp);
  drawDeviceGridButton(kDeviceRightBottomButton, "Volume -", kMdiVolume48Bmp);
}

void renderTvControls() {
  drawDeviceGridButton(kDeviceWideButton, "TV Power", kMdiPower48Bmp, true);
  drawDeviceGridButton(kDeviceLeftTopButton, "Telia", kMdiBox23Bmp);
  drawDeviceGridButton(kDeviceRightTopButton, "PS5", kMdiHdmi48Bmp);
  drawDeviceGridButton(kDeviceLeftBottomButton, "Plex", kMdiTv48Bmp);
  drawDeviceGridButton(kDeviceRightBottomButton, "Netflix", kMdiTv48Bmp);
  drawHintBox("TV USES INPUT + POWER ONLY");
}

void renderLs60Controls() {
  drawDeviceGridButton(kDeviceWideButton, "Unity 71", kMdiSpeaker48Bmp, true);
  drawDeviceGridButton(kDeviceLeftTopButton, "Coax", kMdiHdmi48Bmp);
  drawDeviceGridButton(kDeviceRightTopButton, "Vol 71", kMdiVolume48Bmp);
  drawDeviceGridButton(kDeviceLeftBottomButton, "Vol +", kMdiVolume48Bmp);
  drawDeviceGridButton(kDeviceRightBottomButton, "Vol -", kMdiVolume48Bmp);
  drawHintBox("RECOVERY ONLY - WIIM IS NORMAL VOLUME");
}

void drawTouchTarget(int32_t x, int32_t y, const String &label) {
  constexpr int32_t size = 96;
  constexpr int32_t half = size / 2;
  g_epaper.drawRect(x - half, y - half, size, size, BBEP_BLACK);
  g_epaper.drawRect(x - half + 4, y - half + 4, size - 8, size - 8, BBEP_BLACK);
  g_epaper.drawLine(x - 34, y, x + 34, y, BBEP_BLACK);
  g_epaper.drawLine(x, y - 34, x, y + 34, BBEP_BLACK);
  drawText(label, x - 18, y + 54);
}

void drawGrayscaleTestPage() {
  const int32_t w = g_epaper.width();
  const int32_t h = g_epaper.height();
  const int32_t margin = 20;

  Serial.println("Rendering labelled 4-bit grayscale calibration page with FastEPD...");
  g_epaper.setMode(BB_MODE_4BPP);
  g_epaper.fillScreen(15); // 0=black, 15=white in 4-bit mode.
  g_epaper.setFont(FONT_12x16);
  g_epaper.setTextColor(0, 15);

  g_epaper.drawRect(margin, margin, w - 2 * margin, h - 2 * margin, 0);
  drawText("Lily Remote", 40, 48);
  drawText("4-bit grayscale calibration", 40, 76);
  drawText("Check monotonicity + bad tones", 40, 104);

  // Numbered raw 0-15 swatches so visual anomalies can be tied to exact levels.
  const int32_t swatchX = 42;
  const int32_t swatchY = 142;
  const int32_t swatchW = 300;
  const int32_t swatchH = 28;
  drawText("Raw levels", swatchX, swatchY - 20);
  for (int level = 0; level < 16; ++level) {
    const int32_t y = swatchY + level * swatchH;
    g_epaper.fillRect(swatchX, y, swatchW, swatchH - 2, level);
    g_epaper.drawRect(swatchX, y, swatchW, swatchH - 2, level < 8 ? 15 : 0);
    g_epaper.setTextColor(0, 15);
    drawText(String(level), swatchX + swatchW + 22, y + 6);
  }

  // Candidate UI palette: coarse, intentionally separated tones.
  const uint8_t uiLevels[] = {0, 2, 4, 7, 9, 11, 13, 15};
  const int32_t palX = 42;
  const int32_t palY = 630;
  const int32_t palW = 56;
  const int32_t palH = 96;
  drawText("Candidate UI palette", palX, palY - 24);
  for (uint8_t i = 0; i < sizeof(uiLevels); ++i) {
    const uint8_t level = uiLevels[i];
    g_epaper.fillRect(palX + i * palW, palY, palW - 2, palH, level);
    g_epaper.drawRect(palX + i * palW, palY, palW - 2, palH, level < 8 ? 15 : 0);
  }
  g_epaper.setTextColor(0, 15);
  drawText("0 2 4 7 9 11 13 15", palX, palY + palH + 24);

  // Raw gradient vs remapped gradient. If one band is wrong, this makes it obvious.
  const int32_t gradX = 42;
  const int32_t gradY = 790;
  const int32_t gradW = 456;
  const int32_t gradH = 64;
  drawText("Raw 0-15 gradient", gradX, gradY - 20);
  for (int x = 0; x < gradW; ++x) {
    const uint8_t level = static_cast<uint8_t>((x * 15) / (gradW - 1));
    g_epaper.drawLine(gradX + x, gradY, gradX + x, gradY + gradH, level);
  }
  g_epaper.drawRect(gradX, gradY, gradW, gradH, 0);

  const int32_t remapY = gradY + gradH + 48;
  drawText("Remapped coarse gradient", gradX, remapY - 20);
  for (int x = 0; x < gradW; ++x) {
    const uint8_t idx = static_cast<uint8_t>((x * (sizeof(uiLevels) - 1)) / (gradW - 1));
    g_epaper.drawLine(gradX + x, remapY, gradX + x, remapY + gradH, uiLevels[idx]);
  }
  g_epaper.drawRect(gradX, remapY, gradW, gradH, 0);

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper grayscale calibration refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}
} // namespace

bool initRemoteDisplay() {
  if (g_displayReady) {
    return true;
  }

  if (!psramFound()) {
    Serial.println("Display init failed: PSRAM not found");
    return false;
  }

  Serial.println("Initializing FastEPD for portrait EPDiy V7 / T5 S3 Pro Lite panel...");
  int result = g_epaper.initPanel(BB_PANEL_EPDIY_V7, kPanelBusSpeedHz);
  if (result != BBEP_SUCCESS) {
    Serial.printf("Display initPanel failed: %d\n", result);
    return false;
  }

  result = g_epaper.setPanelSize(kNativeDisplayWidth, kNativeDisplayHeight);
  if (result != BBEP_SUCCESS) {
    Serial.printf("Display setPanelSize failed: %d\n", result);
    return false;
  }

  g_epaper.setMode(BB_MODE_1BPP);
  result = g_epaper.setRotation(kPortraitRotation);
  if (result != BBEP_SUCCESS) {
    Serial.printf("Display setRotation(%d) failed: %d\n", kPortraitRotation, result);
    return false;
  }

  g_epaper.setFont(FONT_12x16);
  g_epaper.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);
  g_epaper.setTextWrap(false);
  g_displayReady = true;

  Serial.printf("Display initialized: FastEPD EPDiy V7, native=%dx%d, rotated=%dx%d, rotation=%d\n",
                kNativeDisplayWidth,
                kNativeDisplayHeight,
                g_epaper.width(),
                g_epaper.height(),
                kPortraitRotation);
  return true;
}

void renderTouchTestPage() {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  const int32_t w = g_epaper.width();
  const int32_t h = g_epaper.height();
  const int32_t margin = 20;
  const int32_t targetInset = 82;

  Serial.println("Rendering portrait touch-test page with FastEPD...");
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  g_epaper.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);
  g_epaper.setTextWrap(false);

  g_epaper.drawRect(margin, margin, w - 2 * margin, h - 2 * margin, BBEP_BLACK);
  drawHLine(margin, 132, w - 2 * margin);
  drawHLine(margin, h - 122, w - 2 * margin);

  drawText("Lily Remote", 40, 48);
  drawText("TOUCH TEST", 40, 76);
  drawText("USB / charging port DOWN", 40, 104);

  drawWrappedText("Tap each target: TL, TR, BL, BR. Then swipe left/right. Serial monitor prints raw and mapped coordinates.",
                  40,
                  158,
                  52,
                  4);

  drawTouchTarget(targetInset, 292, "TL");
  drawTouchTarget(w - targetInset, 292, "TR");
  drawTouchTarget(targetInset, h - 236, "BL");
  drawTouchTarget(w - targetInset, h - 236, "BR");

  g_epaper.drawRect(96, 500, w - 192, 118, BBEP_BLACK);
  drawText("Swipe horizontally here", 144, 548);
  g_epaper.drawLine(146, 590, w - 146, 590, BBEP_BLACK);
  g_epaper.drawLine(146, 590, 174, 568, BBEP_BLACK);
  g_epaper.drawLine(146, 590, 174, 612, BBEP_BLACK);
  g_epaper.drawLine(w - 146, 590, w - 174, 568, BBEP_BLACK);
  g_epaper.drawLine(w - 146, 590, w - 174, 612, BBEP_BLACK);

  drawText("BOTTOM LEFT", 32, h - 86);
  drawText("BOTTOM RIGHT", w - 172, h - 86);
  drawText("SERIAL MONITOR ACTIVE", 138, h - 48);

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper touch-test refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}

void renderSafeControlPage(const RemoteSafeControlPage &page) {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  const int32_t w = g_epaper.width();
  const int32_t h = g_epaper.height();
  const int32_t margin = 20;

  Serial.println("Rendering safe dummy-control page with FastEPD...");
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  g_epaper.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);
  g_epaper.setTextWrap(false);

  g_epaper.drawRect(margin, margin, w - 2 * margin, h - 2 * margin, BBEP_BLACK);
  drawHLine(margin, 132, w - 2 * margin);
  drawHLine(margin, h - 116, w - 2 * margin);

  drawText("Lily Remote", 40, 48);
  drawText("SAFE CONTROL TEST", 40, 76);
  drawText("Dummy helper only - HA writes are safe", 40, 104);

  drawStatusBox(40, 156, 130, 72, "WiFi", page.status.wifiConnected);
  drawStatusBox(204, 156, 130, 72, "HA", page.status.haApiOk);
  drawStatusBox(368, 156, 130, 72, "Entity", page.status.entityOk);

  int32_t y = 264;
  drawText("Remote summary", 40, y);
  y += 28;
  drawWrappedText(page.status.entityOk ? page.status.entityState : String("Missing entity: ") + page.status.entityId,
                  40,
                  y,
                  52,
                  4);

  y = 402;
  drawText("Dummy helper", 40, y);
  y += 28;
  drawWrappedText(page.helperEntityId + String(" = ") + page.helperState, 40, y, 52, 2);

  const String buttonTitle = page.helperState == "on" ? "Turn dummy OFF" : "Turn dummy ON";
  drawButton(kSafeControlToggleButton, buttonTitle, "Tap here to toggle input_boolean");

  y = 720;
  drawText(page.lastActionOk ? "Last action" : "Last error", 40, y);
  y += 28;
  drawWrappedText(page.message.length() > 0 ? page.message : "Ready", 40, y, 52, 3);

  drawFooterNav(UiPageId::SafeControl, "Swipe left for Activities, right for Status");

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper safe-control refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}

void renderActivitiesPage(const RemoteActivitiesPage &page) {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  Serial.println("Rendering home activities page with FastEPD...");
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  setTextBlack();
  g_epaper.setTextWrap(false);

  constexpr int32_t margin = 20;
  g_epaper.drawRect(margin, margin, kUiScreenWidth - 2 * margin, kUiScreenHeight - 2 * margin, BBEP_BLACK);

  // Top bar, matching the HTML prototype: brand, online pill, small media-off action.
  g_epaper.fillRect(20, 20, 16, 58, BBEP_BLACK);
  drawText8("LILY REMOTE", 54, 42);
  drawText8("LIVING ROOM", 54, 60);
  drawLabelPill(334, 40, 78, 38, page.status.haApiOk ? "ONLINE" : "OFFLINE");
  g_epaper.drawRect(kMediaOffButton.x, kMediaOffButton.y, kMediaOffButton.w, kMediaOffButton.h, BBEP_BLACK);
  g_epaper.fillRect(kMediaOffButton.x, kMediaOffButton.y, kMediaOffButton.w, kMediaOffButton.h, BBEP_BLACK);
  drawMdiIcon(kMdiPower18Bmp, kMediaOffButton.x + 9, kMediaOffButton.y + 10, BBEP_WHITE);
  drawInvertedText("OFF", kMediaOffButton.x + 32, kMediaOffButton.y + 11);
  drawHLine(20, 98, kUiScreenWidth - 40);

  // Hero row.
  drawText16("Home", 40, 142);
  g_epaper.drawRect(368, 122, 132, 76, BBEP_BLACK);
  g_epaper.fillRect(368, 122, 132, 26, BBEP_BLACK);
  drawInvertedText("NOW", 378, 128);
  drawText(page.currentActivity.length() > 0 ? page.currentActivity : "Unknown", 392, 170);
  drawHLine(20, 216, kUiScreenWidth - 40);

  drawActivityButton(kActivityWatchTvButton, "Watch TV", kMdiTv50Bmp);
  drawActivityButton(kActivityPs5Button, "Play PS5", kMdiPlaystation50Bmp);
  drawActivityButton(kActivityMusicButton, "Stream", kMdiMusic50Bmp);
  drawActivityButton(kActivityRecordsButton, "Records", kMdiRecord50Bmp);

  // Open space intentionally left below the four core activity buttons.
  drawQuickStatusChip(40, 788, "TV", "Ready");
  drawQuickStatusChip(197, 788, "WiiM", "Paused");
  drawQuickStatusChip(354, 788, "KEF", "Coax");

  drawFooterNav(UiPageId::Activities, "");

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper activities page refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}

void renderDeviceControlPage(const RemoteDeviceControlPage &page) {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  Serial.println("Rendering device control page with FastEPD...");
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  setTextBlack();
  g_epaper.setTextWrap(false);

  constexpr int32_t margin = 20;
  g_epaper.drawRect(margin, margin, kUiScreenWidth - 2 * margin, kUiScreenHeight - 2 * margin, BBEP_BLACK);

  drawKisssTopBar(page.status.haApiOk);
  drawText16("Remote", 40, 140);
  drawDeviceTargetBox(page.target);
  drawHLine(20, 206, kUiScreenWidth - 40);
  drawDeviceTabs(page.target);
  drawPanelFrame();

  switch (page.target) {
  case RemoteDeviceTarget::Telia:
    renderTeliaControls();
    break;
  case RemoteDeviceTarget::Wiim:
    renderWiimControls();
    break;
  case RemoteDeviceTarget::Tv:
    renderTvControls();
    break;
  case RemoteDeviceTarget::Ls60:
    renderLs60Controls();
    break;
  }

  drawFooterNav(UiPageId::Media, "");

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper device control page refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}

void renderShellPage(const RemoteShellPage &page, const RemoteDisplayStatus &status) {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  Serial.printf("Rendering shell page '%s' with FastEPD...\n", uiPageName(page.pageId));
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  g_epaper.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);
  g_epaper.setTextWrap(false);

  constexpr int32_t margin = 20;
  g_epaper.drawRect(margin, margin, kUiScreenWidth - 2 * margin, kUiScreenHeight - 2 * margin, BBEP_BLACK);
  drawPageHeader(page.title, page.subtitle);

  drawStatusBox(40, 162, 130, 72, "WiFi", status.wifiConnected);
  drawStatusBox(204, 162, 130, 72, "HA", status.haApiOk);
  drawStatusBox(368, 162, 130, 72, "State", status.entityOk);

  drawShellCard(40, 272, 460, 168, "Current summary", status.entityOk ? status.entityState : String("Missing: ") + status.entityId);
  drawShellCard(40, 470, 460, 148, "Page role", page.primary);
  drawShellCard(40, 648, 460, 118, "Next safe step", page.secondary);

  drawFooterNav(page.pageId, page.footerHint);

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper shell page refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}

void renderStatusPage(const RemoteDisplayStatus &status) {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  const int32_t w = g_epaper.width();
  const int32_t h = g_epaper.height();
  const int32_t margin = 20;

#if REMOTE_ENABLE_GRAYSCALE_TEST
  drawGrayscaleTestPage();
  return;
#endif

  Serial.println("Rendering portrait e-paper status page with FastEPD...");
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  g_epaper.setTextColor(BBEP_BLACK, BBEP_TRANSPARENT);

  g_epaper.drawRect(margin, margin, w - 2 * margin, h - 2 * margin, BBEP_BLACK);
  drawHLine(margin, 118, w - 2 * margin);
  drawHLine(margin, h - 116, w - 2 * margin);

  drawText("Lily Remote", 40, 48);
  drawText("PORTRAIT TEST", 40, 76);
  drawText("USB / charging port DOWN", 40, 100);

  drawStatusBox(40, 144, 130, 72, "Config", status.configOk);
  drawStatusBox(204, 144, 130, 72, "WiFi", status.wifiConnected);
  drawStatusBox(368, 144, 130, 72, "HA", status.haApiOk);

  int32_t y = 252;
  drawText("Network", 40, y);
  y += 28;
  drawWrappedText(status.wifiConnected
                      ? String("WiFi: ") + status.ipAddress + " RSSI " + String(status.rssi) + " dBm"
                      : "WiFi: not connected",
                  40,
                  y,
                  52,
                  2);

  y += 70;
  drawText("Home Assistant", 40, y);
  y += 28;
  drawWrappedText(status.haApiOk
                      ? String("API: ") + status.haMessage
                      : "API: unavailable",
                  40,
                  y,
                  52,
                  2);

  y += 70;
  drawText("Summary", 40, y);
  y += 28;
  drawWrappedText(status.entityOk
                      ? status.entityState
                      : String("Missing entity: ") + status.entityId,
                  40,
                  y,
                  52,
                  5);

  y += 130;
  drawText(String("Write test: ") + (status.writeTestEnabled ? "ENABLED" : "disabled"), 40, y);
  y += 28;
  drawText(String("FW: ") + status.firmwareVersion, 40, y);

  // Explicit bottom markers for portrait orientation with the charging port down.
  drawText("BOTTOM LEFT", 32, h - 82);
  drawText("BOTTOM RIGHT", w - 172, h - 82);
  drawText("USB / CHARGE DOWN", 160, h - 44);

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper full refresh result=%d in %u ms\n",
                result,
                static_cast<unsigned>(elapsed));
}
