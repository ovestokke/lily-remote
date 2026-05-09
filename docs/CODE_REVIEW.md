# Code Review Report

**Date:** 2026-05-09
**Commit:** `225c8f9` — `feat(firmware): add e-ink remote UI prototype`
**Reviewer:** lint
**Scope:** All source files (`src/`, `include/`)
**Updated:** 2026-05-09 — bugs #1 fixed; bugs #2 and #3 were review errors

---

## Summary

No crashes, memory leaks, or security issues found. One bug was real and has been fixed. Two bugs reported as likely bugs turned out to be review errors — the code did not contain those issues. The codebase is clean from a secrets perspective — only example placeholders are committed.

---

## Bugs Fixed

### 1. Double "(disabled)" in activity/device messages — `src/app.cpp`

`g_activityMessage` and `g_deviceControlMessage` were initialized with verbose text that got doubled when `handleActivitiesTouch`/`handleDeviceControlTouch` appended the script name plus "(disabled)".

**Fix applied:** Changed initial values to `"Tap to see what would run."` so the appended message reads cleanly.

---

## Bugs That Were Review Errors (no bug)

### 2. `printHomeAssistantStatus` was said to discard results — no bug

Initial report claimed the function wrote to a local variable discarded on return. Inspection of both committed and working-tree code shows `printHomeAssistantStatus(g_haClient, g_displayStatus)` is called directly — no local variable wrapping. The global `g_displayStatus` is populated correctly.

### 3. `RemoteShellPage` was said to use stale `const char*` — no bug

Initial report claimed `primary/secondary/footerHint` were `const char*` holding pointers to destroyed `String` temporaries. In fact `display.h` already defines these as `String`:

```cpp
struct RemoteShellPage {
  UiPageId pageId = UiPageId::Info;
  const char *title = "Info";
  const char *subtitle = "";
  String primary;      // ← already String
  String secondary;    // ← already String
  String footerHint;   // ← already String
};
```

No stale pointer issue exists.

---

## Remaining Potential Issues

### 4. `getSupportTouchPoint()` called twice per poll — `src/touch.cpp`

```cpp
const uint8_t count = g_touch.getPoint(xs, ys, g_touch.getSupportTouchPoint());
// ...
Serial.printf(" [%u]=(%d,%d)", i, xs[i], ys[i]); // also calls getSupportTouchPoint()
```

Calls `getSupportTouchPoint()` twice per poll. Not a bug but wasteful. Cache the value.

---

### 5. `drawText8` / `drawText16` always restore font even on early return — `src/display.cpp`

```cpp
void drawText8(const String &text, int32_t x, int32_t y) {
    g_epaper.setFont(FONT_8x8);
    drawText(text, x, y);
    g_epaper.setFont(FONT_12x16);    // always runs, even if drawText is skipped
}
```

Harmless in current code but fragile if `drawText` changes font.

---

### 6. `drawGrayscaleTestPage` never resets mode back to `BB_MODE_1BPP` — `src/display.cpp`

After rendering the grayscale calibration page, the display stays in 4BPP mode. All other render functions explicitly call `setMode(BB_MODE_1BPP)` first, so this is safe in practice. It creates an implicit dependency — adding a new render function that skips `setMode` would break.

---

### 7. SensorLib `setInterruptMode` called after `begin()` — `src/touch.cpp`

```cpp
g_touch.setPins(kTouchRst, kTouchIrq);
if (!g_touch.begin(Wire, kGt911Address, kTouchSda, kTouchScl)) { ... }
g_touch.setInterruptMode(LOW_LEVEL_QUERY);
```

The SensorLib/GT911 documentation sometimes expects interrupt mode to be set before `begin()`. Worked in practice, but could cause initialization flakiness on some boards or library versions.

---

### 8. `strlen()` on uninitialized stack if `vsnprintf` fails — `src/log.cpp`

```cpp
char buffer[256];
vsnprintf(buffer, sizeof(buffer), format, args);
// ...
if (len == 0 || buffer[len - 1] != '\n') {  // buffer may be uninitialized if len < 0
    Serial.println();
}
```

Unlikely on ESP32's newlib (which always nul-terminates), but not robust.

---

### 9. `vsnprintf` return value not handled — `src/log.cpp`

If the formatted message exceeds 256 bytes, `vsnprintf` returns ≥256 and the message is silently truncated. For serial debug output this is acceptable.

---

### 10. `UiPageId::TouchTest` is never routed — `src/ui.cpp`

`uiPageName()` handles `TouchTest`, but `makeShellPage()` has no case for it and `renderCurrentPage()` never dispatches to it. Dead code unless routing is added.

---

### 11. Placeholder "TODO" strings in LS60 device actions — `src/app.cpp`

```cpp
case RemoteDeviceTarget::Ls60: return "ls60 volume_up recovery TODO";
case RemoteDeviceTarget::Ls60: return "ls60 volume_down recovery TODO";
```

Non-script return values that appear in UI messages when device controls are tapped. Fine while disabled, but confusing to leave in place.

---

## Design Notes

### 12. No WiFi/HA reconnection logic — `src/app.cpp`

If WiFi drops after boot, `g_displayStatus` stays frozen at the boot-time state. The UI shows stale state until reboot. `haApiOk` guards against bad reads, but safe-control writes silently fail.

---

### 13. Cooldown uncoordinated — `src/app.cpp`

`kServiceCallCooldownMs = 1200` applies only to safe-control toggle. Activity and device controls have no cooldown, acceptable since they're disabled, but design isn't unified.

---

### 14. `kPortraitRotation = 90` may need to be `270` — `src/display.cpp`

Calibration constant with a documented note: "If the next visual check shows top/bottom swapped, change this to 270." Needs on-device verification.

---

### 15. No I2C error recovery in touch init — `src/touch.cpp`

If GT911 is not found, `initRemoteTouch` returns `false` and touch input silently falls through. Acceptable for bringup, but needs a user-visible error path before shipping.

---

### 16. Redundant guard with dead `#else` branch — `src/app.cpp`

```cpp
#if !REMOTE_ENABLE_TOUCH_TEST
    if (g_currentPage != UiPageId::Media || ...) { return; }
    // ...
#else
    (void)event;   // dead when REMOTE_ENABLE_TOUCH_TEST = 0
#endif
```

Harmless but confusing.

---

## Summary Table

| # | File | Severity | Status |
|---|------|----------|--------|
| 1 | `src/app.cpp` | **Bug** | Fixed |
| 2 | `src/app.cpp` | Bug | Review error — not a bug |
| 3 | `src/app.cpp` / `src/display.h` | Bug | Review error — not a bug |
| 4 | `src/touch.cpp` | Minor | Open |
| 5 | `src/display.cpp` | Minor | Open |
| 6 | `src/display.cpp` | Minor | Open |
| 7 | `src/touch.cpp` | Minor | Open |
| 8 | `src/log.cpp` | Minor | Open |
| 9 | `src/log.cpp` | Minor | Open |
| 10 | `src/ui.cpp` | Minor | Open |
| 11 | `src/app.cpp` | Minor | Open |
| 12 | `src/app.cpp` | Design | Open |
| 13 | `src/app.cpp` | Design | Open |
| 14 | `src/display.cpp` | Design | Open |
| 15 | `src/touch.cpp` | Design | Open |
| 16 | `src/app.cpp` | Design | Open |