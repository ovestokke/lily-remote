# Lily Remote battery trend and firmware drain review — 2026-06-14

## Source data

Read-only Home Assistant telemetry was queried through the sibling `homeassistant` pi session. HA parses these values from firmware telemetry (`input_text.lily_remote_power_event`); HA is not calculating the percentages from voltage.

History available for this review starts at 2026-06-04. The last observed external-power/charge period ended on 2026-06-07.

## Current status

Latest raw event seen during the review:

```text
2026-06-14T16:19:41Z
seq=26 ev=idle_sleep wake=<same cycle> cause=ext0 mv=3602 soc=2 disp=2 avg=-58 awake=94692 pwr=0 chg=not charging
```

Current interpretation:

- Display SOC: 2–3% during the review window.
- Raw gauge SOC: 2–3%.
- Voltage: about 3602–3606 mV.
- External power: off.
- Charge state: not charging.
- Last event type: `idle_sleep`.
- Wake cause: `ext0` touch wake.

## Last charge / unplug point

Observed charge/external-power transitions:

- 2026-06-07T11:47:41Z — external power on, `charge done`.
- 2026-06-07T14:03:50Z–14:06:51Z — brief `fast charging`, then `charge done`.
- 2026-06-07T17:52:10Z — last raw event with external power on: `mv=4119 soc=44 disp=100 pwr=1 chg=charge done`.
- 2026-06-07T17:57:10Z — first event after unplug: `mv=4100 soc=43 disp=88 pwr=0 chg=not charging`.

Time from first post-unplug event to the latest event reviewed: about 6.9 days.

## Overall drain trend

From first post-unplug event to latest reviewed event:

```text
2026-06-07T17:57:10Z  4100 mV  raw 43%  display 88%
2026-06-14T16:19:41Z  3602 mV  raw 2%   display 2%
```

Approximate drain:

- Voltage: 4100 mV → 3602 mV = 498 mV over about 6.94 days = ~72 mV/day.
- Raw SOC: 43% → 2% = 41 points over about 6.94 days = ~5.9 raw-SOC points/day.
- Display SOC: 88% → 2% = 86 points over about 6.94 days = ~12.4 display-SOC points/day.

Important caveat: display SOC is intentionally overridden to 100% while the charger reports `charge done`, even though the raw BQ27220 SOC was only 44% at the last powered event. Treat voltage and raw gauge trend as more useful than display percent for diagnosis.

## Support points

```text
2026-06-07T17:57  4100 mV  raw 43  display 88  unplugged
2026-06-08T16:50  3988 mV  raw 39  display 71
2026-06-10T16:46  3812 mV  raw 30  display 30
2026-06-12T16:34  3678 mV  raw 21  display 21
2026-06-13T11:36  3633 mV  raw 7   display 7
2026-06-14T10:07  3606 mV  raw 3   display 3
2026-06-14T16:19  3602 mV  raw 2   display 2
```

## Sleep vs awake pattern

Post-unplug sleep-to-next-wake voltage changes:

```text
sleep ts                  mv    next wake ts              mv    elapsed   drop
2026-06-07T17:57:10Z      4100  2026-06-08T16:48:44Z      4007  22.86 h   93 mV
2026-06-08T16:50:17Z      3988  2026-06-10T16:44:06Z      3831  47.90 h   157 mV
2026-06-10T16:46:48Z      3812  2026-06-12T16:32:52Z      3693  47.77 h   119 mV
2026-06-12T16:34:27Z      3678  2026-06-12T19:38:08Z      3680   3.06 h   -2 mV
2026-06-12T19:39:40Z      3665  2026-06-13T07:54:11Z      3652  12.24 h   13 mV
2026-06-13T07:55:46Z      3639  2026-06-13T11:34:28Z      3647   3.65 h   -8 mV
2026-06-13T11:36:02Z      3633  2026-06-13T15:28:28Z      3641   3.87 h   -8 mV
2026-06-13T15:30:03Z      3624  2026-06-14T10:05:28Z      3622  18.59 h   2 mV
2026-06-14T10:07:00Z      3606  2026-06-14T14:18:01Z      3618   4.18 h   -12 mV
2026-06-14T14:19:35Z      3604  2026-06-14T16:00:31Z      3616   1.68 h   -12 mV
2026-06-14T16:02:23Z      3600  2026-06-14T16:18:07Z      3615   0.26 h   -15 mV
```

Wake-to-next-sleep voltage changes are consistently about 13–19 mV over roughly 90–160 seconds. This looks like active-load voltage sag plus some real active consumption. Several later sleep gaps show voltage rebound, which supports the sag interpretation.

Totals from first post-unplug sleep to latest sleep:

- Net voltage drop: 4100 mV → 3602 mV = 498 mV.
- Sum across sleep gaps: 327 mV over 166.07 hours.
- Sum across awake windows: 171 mV over about 0.31 hours.

Interpretation: the device is not obviously stuck awake. The logged awake windows are short, but the long sleep gaps still account for most of the durable downward trend.

## Is a week normal?

A week from a truly full, healthy e-ink remote battery with only a few actions per day should normally leave far more battery than this. The observed trend is not explained by user actions alone.

Approximate active-use budget:

- Firmware idle timeout is 90 seconds.
- Telemetry shows awake current around 58–67 mA on sleep events.
- One wake/action window costs roughly 60 mA × 90 s / 3600 = 1.5 mAh, plus boot/WiFi/render overhead.
- A few actions per day should be tens of mAh per week, not enough to explain a fall from ~4100 mV to ~3600 mV unless the actual usable battery capacity is very small, the battery was not truly full, or standby current is high.

## Firmware code review

Review scope: `src/app.cpp`, `src/power_manager.cpp`, `src/display.cpp`, `src/touch.cpp`, `src/battery.cpp`, `include/config.example.h`.

### Findings

1. **No evidence of firmware intentionally staying awake in normal operation.**
   - `setupRemoteApp()` records a wake, connects to WiFi/HA, renders, initializes touch, then calls `markActivity()` (`src/app.cpp:1433`–`1492`).
   - `loopRemoteApp()` polls touch and calls `maybeEnterIdleSleep()` every loop (`src/app.cpp:1495`–`1510`).
   - `maybeEnterIdleSleep()` enters sleep after the idle timeout if no recovery hold/external power is active (`src/app.cpp:748`–`775`).
   - HA events show `idle_sleep` with `awake` around 92–162 seconds, consistent with the 90 second idle timeout plus overhead.

2. **WiFi and display are shut down before deep sleep.**
   - Production sleep calls `shutdownWifiForSleep()`, `btStop()` when Bluetooth is enabled, and `shutdownRemoteDisplayForSleep()` before `esp_deep_sleep_start()` (`src/power_manager.cpp:182`–`192`).
   - Display shutdown calls `einkPower(0)` and `deInit()` (`src/display.cpp:601`–`608`).
   - This argues against an obvious WiFi/display-left-on firmware bug.

3. **The touch controller remains active by design for touch wake.**
   - Normal production sleep arms EXT0 on the GT911 IRQ line (`src/power_manager.cpp:108`–`126`).
   - The GT911 sleep command exists only for audit mode and is deliberately not used when touch wake is enabled (`src/power_manager.cpp:53`–`59`, `src/touch.cpp:235`–`256`).
   - This is a plausible standby-drain contributor, but not necessarily a code bug: sleeping the GT911 may break touch wake depending on board wiring/firmware behavior.

4. **I2C pins are not released in production sleep by default.**
   - `REMOTE_TOUCH_SLEEP_RELEASE_I2C` defaults to 0 (`include/config.example.h:58`–`64`).
   - Production sleep releases the I2C bus only when that flag is enabled (`src/power_manager.cpp:187`–`188`).
   - This is a plausible low-power experiment, especially because the board has several I2C devices. It is not proven to be the drain source.

5. **The 100% display value after charge-done can mislead trend interpretation.**
   - `updateBatteryDisplayStatus()` forces display percent to 100 when the charger reports `charge done`, even when raw SOC is lower (`src/app.cpp:522`–`528`).
   - At last charge-done, raw SOC was 44%, display was 100%. The remote may not have been calibrated/full in the gauge sense.

6. **Telemetry upload itself adds active time but does not explain the week-long drop.**
   - Sleep telemetry is uploaded before sleep (`src/app.cpp:680`–`690`), and wake telemetry is flushed after WiFi/HA connection (`src/app.cpp:667`–`676`, `src/app.cpp:1471`–`1474`).
   - This is expected active work per wake. The observed active windows are still short.

### Review conclusion

No clear code path forces the remote to remain awake. The current firmware appears to enter touch deep sleep after about 90 seconds and shuts down WiFi, Bluetooth, and display power before sleeping.

The likely explanations remain:

1. Standby current is higher than expected, possibly due to touch-wake hardware/GT911/RTC-peripheral/I2C rail behavior.
2. Battery/gauge/charge calibration is misleading: charger `charge done` does not mean the BQ27220 raw SOC reached 100%.
3. Actual usable battery capacity may be smaller than assumed or the pack may be degraded/poorly connected.
4. Less likely: an unobserved sleep abort/recovery hold, but HA event history does not currently show this as the main pattern.

## Recommended next checks

1. Run a timer-only power audit with profiles that isolate sleep-drain suspects:
   - baseline cleanup,
   - ESP cleanup,
   - GT911 sleep,
   - e-paper power off,
   - I2C high-Z,
   - full cleanup.
2. Compare voltage drop per 12–24 hour interval per profile. This is already supported by `REMOTE_POWER_AUDIT_MODE` and audit profiles in `include/config.example.h`.
3. For a normal firmware experiment, test `REMOTE_TOUCH_SLEEP_RELEASE_I2C=1` if touch wake remains reliable.
4. Do one long uninterrupted charge and compare charger `charge done`, raw SOC, RM/FCC, and post-unplug voltage. Do not rely only on the display percent.
5. If firmware profiles do not materially change drain, measure sleep current externally or inspect battery/connector/mechanical condition.
