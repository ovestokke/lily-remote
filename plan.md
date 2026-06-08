# Lily Remote Production Touch-Sleep Plan

## Goal

Implement production deep sleep for the LILYGO T5 E-Paper S3 Pro Lite remote without breaking the core remote UX:

```text
idle -> sleep -> tap screen -> wake immediately -> remote usable
```

Normal production sleep must use **GT911 touch IRQ -> ESP32-S3 EXT0 wake**. No periodic timer wake is allowed in normal production sleep.

Timer-only sleep is retained only as an audit/baseline tool for measuring drain. It is not an acceptable remote mode.

## Hard Requirements

1. **Touch wake is the product wake path.**
   - Production sleep wakes from tap/touch only.
   - If touch wake cannot be armed safely, abort sleep and stay awake.
   - Do not silently switch production sleep to timer wake.

2. **No production timer backup.**
   - A remote waking hourly is not a remote.
   - Timer wake may exist only in audit mode.

3. **Do not ship audit mode enabled.**
   - `REMOTE_POWER_AUDIT_MODE` defaults to `0` in `include/config.example.h`.
   - Release builds must fail if audit mode is enabled.

4. **Do not put GT911 to sleep for production touch wake.**
   - The GT911 must remain capable of asserting IRQ while ESP32 is in deep sleep.
   - The GT911 sleep command is only for timer-only audit/baseline sleep unless hardware testing proves otherwise.

5. **Keep EXT0 wake hardware alive.**
   - Do not force `ESP_PD_DOMAIN_RTC_PERIPH` off in production touch sleep.
   - Use `ESP_PD_OPTION_AUTO` first; use `ON` only if needed for reliable EXT0/pulls.

6. **Recovery is physical/debug, not timer.**
   - USB/reset recovery must stay reliable.
   - On cold reset or external power, stay awake long enough to flash/debug.
   - If repeated fast EXT0 wake loops are detected, stay awake instead of re-entering sleep.

7. **Secrets stay safe.**
   - Do not commit `include/config.h`.
   - Do not commit WiFi credentials, HA tokens, local HA URLs, or local IP secrets.

## Existing Work to Preserve

Current uncommitted changes are useful and should be preserved/refined:

- Audit profile support in `src/app.cpp`, `src/power_manager.*`, and `include/config.example.h`.
- `shutdownRemoteDisplayForSleep()` in `src/display.*`.
- `shutdownRemoteI2cBusForSleep()` and I2C init tracking in `src/i2c_bus.*`.
- GT911 sleep command helper in `src/touch.*`, but rename/wrap it as production-neutral audit support.

Do not revert these just because they were created during audit work.

## Architecture

Use two separate sleep paths.

### 1. Audit timer sleep

Purpose: measure low-drain baseline and isolate hardware/peripheral drain.

Allowed:

- Timer wake.
- GT911 sleep command.
- Display power off/deinit.
- I2C `Wire.end()` and SDA/SCL high-Z.
- RTC peripherals off if compatible with timer wake.
- Audit telemetry fields: `audit=`, `prof=`, `amin=`.

Not allowed:

- Being called by normal UI idle sleep.
- Being shipped as the product UX.

### 2. Production touch sleep

Purpose: real remote sleep.

Required:

- EXT0 wake from GT911 IRQ.
- No timer wake.
- GT911 remains wake-capable.
- Sleep is aborted if touch IRQ cannot be cleared or EXT0 cannot be armed.
- Production telemetry says `policy=touch`, not audit fields.

## Production Touch Sleep Flow

The key design is **preflight before commit**.

Do not shut down WiFi/display/I2C until touch wake has been validated and EXT0 has been armed.

Conceptual flow:

```cpp
bool PowerManager::enterTouchSleep(const char *reason) {
  // Preflight while the system is still fully alive.
  if (!clearRemoteTouchForSleep(REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS)) {
    logPrintf(LogLevel::Warn, "Touch not clear; aborting sleep");
    return false;
  }

  if (!waitForTouchWakeInactive(REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS)) {
    logPrintf(LogLevel::Warn, "Touch IRQ active; aborting sleep");
    return false;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  const gpio_num_t wakeGpio = static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO);
  const esp_err_t err = esp_sleep_enable_ext0_wakeup(wakeGpio, REMOTE_TOUCH_WAKE_LEVEL);
  if (err != ESP_OK) {
    logPrintf(LogLevel::Error, "EXT0 arm failed: %d; aborting sleep", static_cast<int>(err));
    return false;
  }

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);

  // Commit point: after this, we intend to sleep.
  recordProductionSleepTelemetry(reason, "touch");
  renderSleepPage();

  shutdownWifiForSleep();
#if defined(CONFIG_BT_ENABLED)
  btStop();
#endif

  shutdownRemoteDisplayForSleep();

  // Only enable this in production after validation proves GT911 IRQ still works
  // and drain improves or does not regress.
#if REMOTE_TOUCH_SLEEP_RELEASE_I2C
  shutdownRemoteI2cBusForSleep();
#endif

  Serial.flush();
  esp_deep_sleep_start();
  return true; // normally not reached
}
```

If `enterTouchSleep(...)` returns `false`, the app must stay awake, log/report the failure if possible, and retry only after normal user/activity handling. It must not immediately spin in sleep attempts.

## Touch/GT911 Work

Add production sleep-preflight helpers in `src/touch.h/cpp`:

```cpp
bool clearRemoteTouchForSleep(uint32_t timeoutMs);
bool sleepRemoteTouchController();
bool sleepRemoteTouchControllerForPowerAudit(); // wrapper for audit compatibility
```

### `clearRemoteTouchForSleep(timeoutMs)`

This should be concrete, not gesture-based.

Preferred implementation:

1. Ensure I2C is initialized.
2. Read GT911 touch status register, likely `0x814E`.
3. If active/touched, write `0` to clear status.
4. Repeat until status shows no active points or timeout expires.
5. Wait for IRQ GPIO to become inactive.
6. Return `true` only if both GT911 status and IRQ are inactive.

Do not rely solely on `pollRemoteTouch(...)` unless verified that it reads and clears the GT911 status register correctly.

### GT911 sleep command

For timer-only audit sleep only:

1. Hold INT/IRQ low.
2. Send GT911 sleep command.
3. Release INT/IRQ afterward:

```cpp
pinMode(kTouchIrq, INPUT_PULLUP); // or INPUT if external pullup is verified
```

Leaving IRQ driven low after the command can cause wake/drain/debug problems.

## ESP32-S3 Hardware Checks

Before trusting production touch sleep, verify on this board:

1. GPIO `3` is valid for EXT0 wake on ESP32-S3 in this board package.
2. GPIO `3`/GT911 IRQ does not interfere with boot/reset strapping on this board.
3. `REMOTE_TOUCH_WAKE_LEVEL` is correct; current assumption is active-low (`0`).
4. EXT0 wake is reliable with `ESP_PD_DOMAIN_RTC_PERIPH` set to `AUTO`.
5. If using internal pullups/holds, test whether `RTC_PERIPH` must be `ON` instead.

## I2C Shutdown Strategy

I2C high-Z helped the timer-only audit baseline, but production touch wake must prove it is safe.

Implement a config switch:

```cpp
#define REMOTE_TOUCH_SLEEP_RELEASE_I2C 0
```

Validation sequence:

1. First production touch-sleep build: leave I2C alive (`0`).
2. Verify 100/100 touch wake cycles.
3. Run drain test.
4. Then test `REMOTE_TOUCH_SLEEP_RELEASE_I2C 1`.
5. Keep it enabled only if touch wake remains 100/100 reliable and drain improves or does not regress.

## Config Defaults

In `include/config.example.h`:

```cpp
#define REMOTE_IDLE_SLEEP_ENABLED 1
#define REMOTE_IDLE_SLEEP_TIMEOUT_MS (90UL * 1000UL)

#define REMOTE_SLEEP_WAKE_POLICY REMOTE_SLEEP_WAKE_TOUCH
#define REMOTE_SLEEP_WAKE_TOUCH 1

#define REMOTE_TOUCH_WAKE_GPIO 3
#define REMOTE_TOUCH_WAKE_LEVEL 0
#define REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS 1500
#define REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS 1500
#define REMOTE_TOUCH_SLEEP_RELEASE_I2C 0

#define REMOTE_POWER_AUDIT_MODE 0
#define REMOTE_POWER_AUDIT_PROFILE 0
```

No production `REMOTE_SLEEP_TIMER_BACKUP_SECONDS` should be added.

## Power Manager API

Avoid fake abstractions. If there is only one production policy, do not add a one-value enum.

Preferred API:

```cpp
class PowerManager {
public:
  explicit PowerManager(uint32_t sleepAfterBootSeconds = 60);

  void maybeSleepAfterBoot(bool enabled);       // existing/test behavior
  bool enterTouchSleep(const char *reason);     // production path
  void goToSleep();                             // compatibility wrapper calls enterTouchSleep("manual")
  void auditSleep(uint32_t timerSeconds, bool enableTouchWake, uint8_t auditProfile);
};
```

`auditSleep(...)` remains separate and must not be used by normal idle sleep.

## App Integration

Current path:

```cpp
maybeEnterIdleSleep()
  -> enterRemoteSleep("idle")
  -> g_powerManager.goToSleep()
```

Change to:

```cpp
maybeEnterIdleSleep()
  -> enterRemoteSleep("idle")
  -> g_powerManager.enterTouchSleep("idle")
```

Requirements:

- Record telemetry before the commit point, but after preflight succeeds.
- If sleep aborts, record/log `sleep_abort` with reason such as `touch_irq_active` or `ext0_arm_failed`.
- After sleep abort, update `g_lastActivityMs` or equivalent so the firmware does not immediately retry sleep in a tight loop.
- Normal production telemetry must not include `audit=`, `prof=`, or `amin=`.

## Recovery Guardrails

Add RTC counters in `src/app.cpp`:

```cpp
uint32_t consecutiveExt0Wakes;
uint32_t consecutiveFastWakes;
uint32_t sleepAbortCount;
```

Behavior:

- Increment EXT0 counters on EXT0 wake.
- Detect fast wake loops using previous awake/sleep duration.
- If repeated fast EXT0 wakes occur, stay awake for a recovery window instead of sleeping again.
- On external power or non-deep-sleep reset, stay awake long enough for flashing/debugging.

This is not a timer wake fallback. It only affects behavior after the device has already woken or reset.

## Display Sleep Page

Before production sleep, render a clear static page:

```text
Sleeping — tap to wake
```

Optional small text:

```text
fw <version>
```

If sleep is aborted, render/log something distinct only if useful:

```text
Sleep aborted — touch wake not ready
```

Do not spend power on animations or repeated redraws.

## Build Hygiene

Add release guard in code after config include:

```cpp
#if defined(REMOTE_BUILD_RELEASE) && REMOTE_POWER_AUDIT_MODE != 0
#error "Release builds must not enable REMOTE_POWER_AUDIT_MODE"
#endif
```

Add release PlatformIO env carefully. Preserve board-specific flags:

```ini
[env:t5_epaper_s3_lite_release]
extends = t5_epaper_s3_lite
build_flags =
  ${env:t5_epaper_s3_lite.build_flags}
  -DREMOTE_BUILD_RELEASE=1
```

Avoid adding a second conflicting `CORE_DEBUG_LEVEL` unless the base flag is removed/refactored.

## Implementation Order

1. Preserve current audit-profile changes.
2. Add config defaults for production touch sleep.
3. Add `clearRemoteTouchForSleep(...)` using GT911 register clear.
4. Rename/wrap GT911 sleep command helper and release IRQ after command.
5. Add `PowerManager::enterTouchSleep(...)` with strict preflight-before-commit flow.
6. Wire idle sleep to `enterTouchSleep("idle")`.
7. Add sleep-abort handling so failed preflight does not loop.
8. Add RTC wake-loop/recovery counters.
9. Add release build guard/env.
10. Build and test.

## Static Verification

Run:

```bash
pio run
pio run -e t5_epaper_s3_lite_release
rg -n "REMOTE_SLEEP_TIMER_BACKUP|TouchWakeWithTimerBackup|timer fallback|fallback timer" src include plan.md
rg -n "auditSleep\(" src
rg -n "ForPowerAudit" src include
```

Acceptance:

- Both builds pass.
- No production code refers to timer-backup sleep.
- Normal idle sleep does not call `auditSleep(...)`.
- `ForPowerAudit` appears only as an audit compatibility wrapper or not at all.
- Release build cannot compile with audit mode enabled.

## Hardware Test Plan

### 1. USB/reset recovery

1. Flash firmware.
2. Connect USB.
3. Press reset.
4. Confirm serial appears.
5. Confirm firmware stays awake long enough to upload again.

Pass: upload/debug remains possible.

### 2. Touch wake reliability

1. Use production touch sleep with `REMOTE_TOUCH_SLEEP_RELEASE_I2C 0`.
2. Run 100 manual cycles:
   - idle sleep enters
   - tap wakes device
   - UI/touch reinitializes
   - no immediate stale-touch re-sleep
3. Leave untouched long enough to confirm no spontaneous wake.

Pass:

- 100/100 tap wakes.
- No timer wakes.
- No wake loop.
- No dead-looking state.

### 3. Touch wake with I2C release

Only after test 2 passes:

1. Set `REMOTE_TOUCH_SLEEP_RELEASE_I2C 1`.
2. Repeat 100-cycle touch wake test.
3. If any wake reliability regression occurs, keep I2C release disabled for production.

### 4. Production drain

1. Use production touch sleep.
2. Unplug USB.
3. Let sit 24 hours with minimal interaction.
4. Manually wake by touch to collect/report telemetry.
5. Compare voltage slope to timer-only audit baseline.

Pass target:

- <= `20 mV/day` unless battery relaxation explains early drop.
- No unexplained EXT0 wake loop.
- No timer wake while untouched.

### 5. HA/WiFi outage

1. Make HA or WiFi unavailable.
2. Boot/use device on battery.
3. Confirm it does not stay awake forever trying to report.
4. Restore network.

Pass: network outage does not prevent safe touch sleep.

### 6. Long soak

1. Run final candidate 72 hours.
2. Normal intended use.
3. Collect HA telemetry and manual observations.

Pass:

- Touch wake remains reliable.
- Drain is acceptable.
- No reset storm.
- Device remains usable.

## Release Decision

| Result | Decision |
|---|---|
| Touch wake reliability fails | Do not ship production deep sleep. |
| Touch wake passes but drain fails | Do not ship; continue power work. |
| USB/reset recovery fails | Do not ship any deep sleep firmware. |
| HA/WiFi outage keeps device awake forever | Do not ship. |
| Touch wake reliability, drain, recovery, and outage tests pass | Ship production touch sleep. |

## Definition of Done

Done means all are true:

1. Normal idle sleep uses `enterTouchSleep(...)`, not audit sleep.
2. Production sleep has no timer wake/backup.
3. GT911 is not put to sleep in production touch sleep.
4. Touch IRQ is cleared and EXT0 is armed before shutdown commit.
5. Failed touch preflight aborts sleep and does not loop.
6. USB/reset recovery works.
7. Repeated fast EXT0 wakes cause stay-awake recovery.
8. Touch wake passes 100/100 cycle test.
9. Production touch-sleep drain is acceptable.
10. Audit mode is off by default and guarded in release builds.
11. `pio run` and release build pass.
12. No secrets or `include/config.h` are staged or committed.
