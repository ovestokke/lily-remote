# Power Audit Plan

Goal: figure out why the Lily Remote battery appears to drop quickly without opening the glued-shut device.

## Context

- The device cannot be opened for inline current measurement.
- While USB is connected, the device does not enter its normal battery sleep behavior, so USB serial alone is not enough.
- SOC percentage may be unreliable until the BQ27220 fuel gauge is calibrated/learned. Voltage and current trends are more useful than SOC alone.

## Likely suspects

1. **Repeated wakeups on battery**
   - Firmware may enter deep sleep after idle, then immediately wake again due to GT911 touch IRQ/GPIO3 state.

2. **Touch wake drain**
   - Deep sleep with external wake enabled may be disturbed by touch IRQ level/noise or touch controller behavior.

3. **Active WiFi/render cycles**
   - If the device repeatedly wakes, connects WiFi, calls Home Assistant, and renders, drain could be much higher than expected.

4. **Fuel gauge calibration error**
   - Gauge previously showed ~65% while charger/voltage suggested the battery may be fairly full. SOC drops may not directly equal real capacity loss.

## Diagnostic firmware mode

Add a config-gated `REMOTE_ENABLE_POWER_AUDIT` mode that logs/displays:

- boot counter
- wake reason: power-on, timer, touch/ext0, unknown
- battery SOC %
- battery voltage mV
- average current mA, if available
- charger/external power state
- touch IRQ GPIO level before sleep
- uptime before sleep
- firmware mode/test label

Example log payload:

```text
boot=12 mode=touch_wake wake=timer soc=62 voltage=4040mV current=-18mA ext_power=0 irq=1 uptime=3s
sleep_start boot=12 soc=62 voltage=4038mV current=-22mA irq=1
```

Prefer sending this to Home Assistant so results are available after the device has been unplugged.

## Test matrix

### Test A: normal UI idle

- Current behavior: boot UI, allow idle sleep after timeout.
- Purpose: determine real-world drain.

### Test B: timer-only deep sleep

- Disable touch wake.
- Wake by timer every 15–30 minutes.
- Connect WiFi, log battery/wake data to HA, then sleep again.
- If drain is low here, base deep sleep is fine.

### Test C: touch-wake deep sleep

- Enable GT911/GPIO3 wake.
- Also use timer wake as a backup heartbeat.
- Log wake reason and IRQ level.
- If wake reason is repeatedly touch/ext0 without user interaction, touch IRQ is likely the problem.

### Test D: active/no-sleep estimate

- Keep device awake for a short known interval and log current/voltage.
- Purpose: estimate how bad repeated wake cycles would be.

## Interpretation

| Observation | Likely conclusion |
| --- | --- |
| Timer-only sleep barely drains | ESP32 deep sleep is okay |
| Touch-wake mode wakes often | GT911 IRQ / GPIO3 wake issue |
| SOC drops but voltage barely changes | Gauge calibration issue |
| Voltage drops significantly during active cycles | Real power drain, likely repeated wake/WiFi/render |
| Average current high while supposedly sleeping | Sleep is not effective or peripheral remains powered |

## Implementation notes

- Keep mode behind config flags; do not disturb normal firmware.
- Do not read/modify credential files unless needed and approved.
- Since USB prevents normal sleep behavior, tests must be uploaded over USB, then unplugged.
- Use timer wake for unattended logging because serial monitor is unavailable while unplugged.
- Consider writing to Home Assistant `input_text`/sensor helpers, or calling an existing logging script if available.

## First implementation step

Add a small power-audit mode that:

1. boots,
2. reads battery + wake reason + GPIO3 level,
3. sends one HA log entry,
4. renders a minimal status page,
5. enters timer-only deep sleep for 30 minutes.

Then compare overnight SOC/voltage trend against the current normal firmware.
