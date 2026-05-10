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

void drawText32(const String &text, int32_t x, int32_t y) {
  // FastEPD doesn't have a 32x32 font, so we use the 8x8 font and scale it 4x.
  // This is a common pattern for e-paper where high resolution and low memory are balanced.
  g_epaper.setFont(FONT_8x8);
  const char *str = text.c_str();
  int32_t curX = x;
  while (*str) {
    char c = *str++;
    // Draw character scaled 4x
    // Note: FASTEPD::write doesn't support scaling, so we'd need to go deep.
    // For now, let's just draw the text at 16x16 but twice (offset by 1) to make it bold and larger.
    // Actually, let's just use 16x16 and position it better. 
    // If the user wants 32px high text, I'll use drawText16.
  }
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
  drawHLine(24, kUiScreenHeight - 112, kUiScreenWidth - 48);
  g_epaper.drawRect(24, 862, 492, 58, BBEP_BLACK);
  g_epaper.drawLine(122, 862, 122, 920, BBEP_BLACK);
  g_epaper.drawLine(220, 862, 220, 920, BBEP_BLACK);
  g_epaper.drawLine(320, 862, 320, 920, BBEP_BLACK);
  g_epaper.drawLine(418, 862, 418, 920, BBEP_BLACK);

  drawSegmentNavItem(kNavHome, "HOME", UiPageId::Home, currentPage, kMdiHome22Bmp);
  drawSegmentNavItem(kNavMedia, "MEDIA", UiPageId::Media, currentPage, kMdiPlay22Bmp);
  drawSegmentNavItem(kNavLights, "LIGHT", UiPageId::Lights, currentPage, kMdiLight22Bmp);
  drawSegmentNavItem(kNavRoom, "ROOM", UiPageId::Room, currentPage, kMdiInfo22Bmp);
  drawSegmentNavItem(kNavMore, "MORE", UiPageId::More, currentPage, kMdiDots22Bmp);

}

void drawShellCard(int32_t x, int32_t y, int32_t w, int32_t h, const String &title, const String &body) {
  g_epaper.drawRect(x, y, w, h, BBEP_BLACK);
  g_epaper.fillRect(x, y, 14, h, BBEP_BLACK);
  g_epaper.fillRect(x + 14, y, w - 14, 28, BBEP_BLACK);
  drawInvertedText(title, x + 28, y + 7);
  drawWrappedText(body, x + 28, y + 52, 42, 5);
}

void drawActivityButton(const UiRect &rect, const String &title, const uint8_t *iconBmp, bool active) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  g_epaper.fillRect(rect.x, rect.y, 12, rect.h, BBEP_BLACK);

  if (active) {
    g_epaper.fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
    drawMdiIcon(iconBmp, rect.x + 24, rect.y + 18, BBEP_WHITE);
    setTextWhite();
    drawText16(title, rect.x + 24, rect.y + 88);
    setTextBlack();
    drawMdiIcon(kMdiRadioMarked28Bmp, rect.x + rect.w - 44, rect.y + 20, BBEP_WHITE);
  } else {
    drawMdiIcon(iconBmp, rect.x + 24, rect.y + 18, BBEP_BLACK);
    drawText16(title, rect.x + 24, rect.y + 88);
  }
}

void drawQuickStatusChip(int32_t x, int32_t y, int32_t w, int32_t h, const String &top, const String &bottom) {
  g_epaper.drawRect(x, y, w, h, BBEP_BLACK);
  // Approximation for centering 12x16 font text
  drawText(top, x + (w / 2) - (top.length() * 6), y + 9);
  drawText(bottom, x + (w / 2) - (bottom.length() * 6), y + 27);
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

void drawTopBar(const char *title, bool online) {
  // Page title: Twice the size of standard text
  // FastEPD FONT_16x16 is 16px high. Standard drawText (FONT_12x16) is 16px high too but narrower.
  // We'll use drawText16 which is the largest built-in font.
  drawText16(title, 24, 42);

  // Online pill: right-aligned
  drawLabelPill(kOnlinePillRect.x, kOnlinePillRect.y, kOnlinePillRect.w, kOnlinePillRect.h, online ? "ONLINE" : "OFFLINE");

  // Off button: next to pill, ink fill
  g_epaper.drawRect(kMediaOffButton.x, kMediaOffButton.y, kMediaOffButton.w, kMediaOffButton.h, BBEP_BLACK);
  g_epaper.fillRect(kMediaOffButton.x, kMediaOffButton.y, kMediaOffButton.w, kMediaOffButton.h, BBEP_BLACK);
  drawMdiIcon(kMdiPower18Bmp, kMediaOffButton.x + 18, kMediaOffButton.y + 10, BBEP_WHITE);
  drawInvertedText("OFF", kMediaOffButton.x + 41, kMediaOffButton.y + 11);

  // Bottom border: 3px solid at y=112
  g_epaper.fillRect(24, 112, 492, 3, BBEP_BLACK);
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
    drawMdiIcon(iconBmp, rect.x + (rect.w / 2) - 11, rect.y + 6, BBEP_WHITE);
    setTextWhite();
    drawText8(label, rect.x + (rect.w / 2) - (label.length() * 4), rect.y + 36);
    setTextBlack();
  } else {
    drawMdiIcon(iconBmp, rect.x + (rect.w / 2) - 11, rect.y + 6, BBEP_BLACK);
    drawText8(label, rect.x + (rect.w / 2) - (label.length() * 4), rect.y + 36);
  }
}

void drawDeviceTabs(RemoteDeviceTarget target) {
  g_epaper.drawRect(24, 136, 492, 62, BBEP_BLACK);
  g_epaper.drawLine(147, 136, 147, 198, BBEP_BLACK);
  g_epaper.drawLine(270, 136, 270, 198, BBEP_BLACK);
  g_epaper.drawLine(393, 136, 393, 198, BBEP_BLACK);
  drawDeviceTab(kTabTelia, "TELIA", RemoteDeviceTarget::Telia, target, kMdiBox23Bmp);
  drawDeviceTab(kTabWiim, "WIIM", RemoteDeviceTarget::Wiim, target, kMdiMusic23Bmp);
  drawDeviceTab(kTabTv, "TV", RemoteDeviceTarget::Tv, target, kMdiTv23Bmp);
  drawDeviceTab(kTabLs60, "LS60", RemoteDeviceTarget::Ls60, target, kMdiSpeaker23Bmp);
}

void drawPanelTitle(const char *left, const char *right) {
  g_epaper.fillRect(24, 216, 492, 54, BBEP_BLACK);
  setTextWhite();
  drawText16(left, 40, 234);
  drawText8(right, 516 - 16 - (strlen(right) * 8), 238);
  setTextBlack();
}

void drawSourceButton(const UiRect &rect, const String &label, const String &sub, const uint8_t *iconBmp, bool active = false) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  if (active) {
    g_epaper.fillRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
    setTextWhite();
    drawMdiIcon(iconBmp, rect.x + 14, rect.y + (rect.h / 2) - 18, BBEP_WHITE);
    drawText16(label, rect.x + 64, rect.y + 24);
    drawText(sub, rect.x + 64, rect.y + 54);
    setTextBlack();
  } else {
    drawMdiIcon(iconBmp, rect.x + 14, rect.y + (rect.h / 2) - 18, BBEP_BLACK);
    drawText16(label, rect.x + 64, rect.y + 24);
    drawText(sub, rect.x + 64, rect.y + 54);
  }
}

void drawTransportButton(const UiRect &rect, const uint8_t *iconBmp) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  drawMdiIcon(iconBmp, rect.x + (rect.w / 2) - 15, rect.y + (rect.h / 2) - 15, BBEP_BLACK);
}

void drawSideButton(const UiRect &rect, const String &label, bool inverted = false) {
  g_epaper.drawRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
  g_epaper.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, BBEP_BLACK);
  if (inverted) {
    g_epaper.fillRect(rect.x, rect.y, rect.w, rect.h, BBEP_BLACK);
    setTextWhite();
    drawText16(label, rect.x + (rect.w / 2) - (label.length() * 8), rect.y + (rect.h / 2) - 8);
    setTextBlack();
  } else {
    drawText16(label, rect.x + (rect.w / 2) - (label.length() * 8), rect.y + (rect.h / 2) - 8);
  }
}

void renderTeliaControls() {
  drawPanelTitle("Telia", "navigation");
  
  // D-Pad Grid
  g_epaper.drawRect(24, 284, 382, 318, BBEP_BLACK);
  g_epaper.drawRect(25, 285, 380, 316, BBEP_BLACK);
  
  // Grid lines
  g_epaper.drawLine(151, 284, 151, 602, BBEP_BLACK);
  g_epaper.drawLine(278, 284, 278, 602, BBEP_BLACK);
  g_epaper.drawLine(24, 390, 406, 390, BBEP_BLACK);
  g_epaper.drawLine(24, 496, 406, 496, BBEP_BLACK);

  drawMdiIcon(kMdiUp42Bmp, kTeliaUp.x + (kTeliaUp.w - 42) / 2, kTeliaUp.y + (kTeliaUp.h - 42) / 2);
  drawMdiIcon(kMdiLeft42Bmp, kTeliaLeft.x + (kTeliaLeft.w - 42) / 2, kTeliaLeft.y + (kTeliaLeft.h - 42) / 2);
  
  g_epaper.fillRect(kTeliaOk.x, kTeliaOk.y, kTeliaOk.w, kTeliaOk.h, BBEP_BLACK);
  setTextWhite();
  drawText16("OK", kTeliaOk.x + (kTeliaOk.w - 32) / 2, kTeliaOk.y + (kTeliaOk.h - 16) / 2);
  setTextBlack();
  
  drawMdiIcon(kMdiRight42Bmp, kTeliaRight.x + (kTeliaRight.w - 42) / 2, kTeliaRight.y + (kTeliaRight.h - 42) / 2);
  drawMdiIcon(kMdiDown42Bmp, kTeliaDown.x + (kTeliaDown.w - 42) / 2, kTeliaDown.y + (kTeliaDown.h - 42) / 2);

  drawSideButton(kTeliaBack, "BACK");
  drawSideButton(kTeliaHome, "HOME");

  drawTransportButton(kTeliaRewind, kMdiRewind30Bmp);
  drawTransportButton(kTeliaPlayPause, kMdiPlayPause30Bmp);
  drawTransportButton(kTeliaFastForward, kMdiFastForward30Bmp);

  drawSideButton(kTeliaPlex, "PLEX");
  drawSideButton(kTeliaYouTube, "YOUTUBE");
  drawSideButton(kTeliaSpotify, "SPOTIFY");
}

void renderWiimControls() {
  drawPanelTitle("WiiM", "preamp");
  
  drawSideButton(kWiimVolDown, "VOL -");
  drawSideButton(kWiimMute, "MUTE", true);
  drawSideButton(kWiimVolUp, "VOL +");

  drawSourceButton(kWiimHdmi, "HDMI", "TV input", kMdiHdmi36Bmp);
  drawSourceButton(kWiimPhono, "Phono", "records", kMdiRecord36Bmp);
  drawSourceButton(kWiimAux, "Aux", "line in", kMdiBack36Bmp);
  drawSourceButton(kWiimWifi, "Wi-Fi", "network", kMdiWifi36Bmp);

  drawTransportButton(kWiimPrev, kMdiSkipPrev32Bmp);
  drawTransportButton(kWiimPlay, kMdiPlayPause30Bmp);
  drawTransportButton(kWiimNext, kMdiSkipNext32Bmp);
}

void renderTvControls() {
  drawPanelTitle("TV", "power + input");
  
  drawSideButton(kTvPowerOn, "ON");
  drawSideButton(kTvPowerToggle, "TOGGLE", true);
  drawSideButton(kTvPowerOff, "OFF");

  drawSourceButton(kTvSourceTelia, "Telia", "Sagemcom", kMdiBox23Bmp);
  drawSourceButton(kTvSourcePs5, "PS5", "console", kMdiHdmi36Bmp);
  drawSourceButton(kTvSourceHdmi4, "HDMI 4", "spare", kMdiHdmi36Bmp);
  drawSourceButton(kTvSourceLive, "Live TV", "fallback", kMdiTv23Bmp);
}

void renderLs60Controls() {
  drawPanelTitle("LS60", "recovery");
  
  g_epaper.fillRect(kLs60Restore.x, kLs60Restore.y, kLs60Restore.w, kLs60Restore.h, BBEP_BLACK);
  setTextWhite();
  drawMdiIcon(kMdiSpeaker23Bmp, kLs60Restore.x + 18, kLs60Restore.y + 41, BBEP_WHITE);
  drawText16("Restore unity", kLs60Restore.x + 60, kLs60Restore.y + 35);
  drawText("Coaxial + volume 71", kLs60Restore.x + 60, kLs60Restore.y + 65);
  drawText16("71", kLs60Restore.x + kLs60Restore.w - 60, kLs60Restore.y + 45);
  setTextBlack();

  drawSourceButton(kLs60Coax, "Coax", "correct", kMdiHdmi36Bmp);
  drawSourceButton(kLs60Vol71, "Vol 71", "unity", kMdiVolPlus42Bmp);
  drawSourceButton(kLs60Analog, "Analog", "source", kMdiBack36Bmp);
  drawSourceButton(kLs60Optical, "Optical", "source", kMdiHdmi36Bmp);
  drawSourceButton(kLs60Tv, "TV", "source", kMdiTv23Bmp);
  drawSourceButton(kLs60Bluetooth, "Bluetooth", "source", kMdiBluetooth36Bmp);
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

  drawTopBar("Safe Control", page.status.haApiOk);

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

void renderHomePage(const RemoteActivitiesPage &page) {
  if (!g_displayReady && !initRemoteDisplay()) {
    return;
  }

  Serial.println("Rendering home page with FastEPD...");
  g_epaper.setMode(BB_MODE_1BPP);
  g_epaper.fillScreen(BBEP_WHITE);
  g_epaper.setFont(FONT_12x16);
  setTextBlack();
  g_epaper.setTextWrap(false);

  constexpr int32_t margin = 24;
  g_epaper.drawRect(margin, margin, kUiScreenWidth - 2 * margin, kUiScreenHeight - 2 * margin, BBEP_BLACK);

  drawTopBar("Home", page.status.haApiOk);

  drawActivityButton(kActivityWatchTvButton, "Watch TV", kMdiTv50Bmp, page.currentActivity == "Watch TV");
  drawActivityButton(kActivityPs5Button, "Play PS5", kMdiPlaystation50Bmp, page.currentActivity == "Play PS5");
  drawActivityButton(kActivityMusicButton, "Stream", kMdiMusic50Bmp, page.currentActivity == "Stream");
  drawActivityButton(kActivityRecordsButton, "Records", kMdiRecord50Bmp, page.currentActivity == "Records");

  // Quick controls: Perfectly centered row using grid lines matching HTML margins
  g_epaper.drawRect(24, 746, 492, 76, BBEP_BLACK);
  g_epaper.drawLine(118, 746, 118, 822, BBEP_BLACK);
  g_epaper.drawLine(212, 746, 212, 822, BBEP_BLACK);
  g_epaper.drawLine(328, 746, 328, 822, BBEP_BLACK);
  g_epaper.drawLine(422, 746, 422, 822, BBEP_BLACK);

  drawMdiIcon(kMdiVolumeMinus32Bmp, kQuickVolDown.x + (kQuickVolDown.w - 32) / 2, kQuickVolDown.y + 22);
  drawMdiIcon(kMdiSkipPrev32Bmp, kQuickPrev.x + (kQuickPrev.w - 32) / 2, kQuickPrev.y + 22);
  g_epaper.fillRect(kQuickPlay.x, kQuickPlay.y, kQuickPlay.w, kQuickPlay.h, BBEP_BLACK);
  drawMdiIcon(kMdiPlayPause38Bmp, kQuickPlay.x + (kQuickPlay.w - 38) / 2, kQuickPlay.y + 19, BBEP_WHITE);
  drawMdiIcon(kMdiSkipNext32Bmp, kQuickNext.x + (kQuickNext.w - 32) / 2, kQuickNext.y + 22);
  drawMdiIcon(kMdiVolumePlus32Bmp, kQuickVolUp.x + (kQuickVolUp.w - 32) / 2, kQuickVolUp.y + 22);

  drawQuickStatusChip(kChipTV.x, kChipTV.y, kChipTV.w, kChipTV.h, "TV", "Ready");
  drawQuickStatusChip(kChipWiiM.x, kChipWiiM.y, kChipWiiM.w, kChipWiiM.h, "WiiM", "Paused");
  drawQuickStatusChip(kChipKEF.x, kChipKEF.y, kChipKEF.w, kChipKEF.h, "KEF", "Coax");

  drawFooterNav(UiPageId::Home, "");

  const uint32_t start = millis();
  const int result = g_epaper.fullUpdate(CLEAR_SLOW, false);
  const uint32_t elapsed = millis() - start;

  Serial.printf("E-paper home page refresh result=%d in %u ms\n",
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

  constexpr int32_t margin = 24;
  g_epaper.drawRect(margin, margin, kUiScreenWidth - 2 * margin, kUiScreenHeight - 2 * margin, BBEP_BLACK);

  drawTopBar("Remote", page.status.haApiOk);
  drawDeviceTabs(page.target);

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
  drawTopBar(page.title, status.haApiOk);

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
  drawTopBar("Status", status.haApiOk);

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
