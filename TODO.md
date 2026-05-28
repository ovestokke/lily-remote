# Lily Remote TODO

Roadmap from the current working PoC to an end-game Home Assistant remote that can control the living room reliably.

## Current progress

- [x] PlatformIO project builds for LILYGO T5 E-Paper S3 Pro Lite / ESP32-S3.
- [x] USB serial/upload works via `/dev/ttyACM0`.
- [x] WiFi connects successfully.
- [x] Home Assistant REST auth works.
- [x] Read-only HA state fetch works using `sensor.remote_summary`.
- [x] Safe HA write/service-call test works using `input_boolean.lily_remote_test`.
- [x] Write test is guarded by `REMOTE_ENABLE_HA_WRITE_TEST` and disabled by default.
- [x] HA test helpers are managed in the sibling `../homeassistant` repo.

## Rules / constraints

- [ ] Remote must talk only to Home Assistant, never directly to TV/KEF/WiiM/Hue.
- [ ] Keep secrets only in local `include/config.h`; never commit tokens/passwords.
- [ ] Home Assistant config changes must be made in `../homeassistant`, not directly here.
- [ ] Use read-only HA tests before enabling real service calls.
- [ ] Prefer reliable e-ink interactions over animations.
- [ ] Full refresh on page transitions; partial refresh for small status changes.

## Phase 1 — Hardware discovery

- [x] Add firmware mode or sketch for I2C scan.
- [x] Record detected I2C addresses in `docs/HARDWARE.md`.
- [x] Confirm touch controller address: GT911 at `0x5D`.
- [x] Confirm RTC address/family: PCF85063/PCF8563 family at `0x51`.
- [x] Confirm power/battery/charger chips: BQ25896 `0x6B`, BQ27220 `0x55`, TPS65185 `0x68`.
- [ ] Confirm board revision/silkscreen labels.
- [x] Document any unknown devices: none from current scan; `0x20` likely PCA9535 IO expander.

## Phase 2 — E-paper display bring-up

- [x] Add vendor/display library dependency.
- [x] Run minimal display init.
- [x] Clear screen to white.
- [x] Draw black text: firmware version, IP address, HA status.
- [x] Confirm native resolution: `960x540`.
- [x] Confirm rotation/orientation visually on device: portrait mode, charging port down, FastEPD rotation `90`.
- [x] Confirm black/white rendering quality visually on device: text readable, contrast OK, no artifacts observed.
- [x] Test 16-grayscale support if library supports it: works technically, but full 16-level output is not visually reliable; raw level `6` is anomalously dark and dark tones are compressed/noisy, so core UI should use black/white plus a small safe gray subset only.
- [x] Document refresh time and visible artifacts.
- [x] Add a simple display abstraction module.

## Phase 3 — Touch bring-up

- [x] Add GT911/touch driver.
- [x] Add visible touch-test page with corner targets and swipe zone.
- [x] Print touch events to serial and verify with real taps.
- [x] Record raw coordinates for each screen corner: TL ≈ `(76,293)`, TR ≈ `(458,303)`, BL ≈ `(84,727)`, BR ≈ `(450,732)` on the test targets.
- [x] Map touch coordinates to display coordinates: identity mapping after portrait rotation; clamp to logical `540x960`.
- [x] Confirm orientation matches display rotation.
- [x] Detect tap down/up.
- [x] Detect long press.
- [x] Detect horizontal swipe with debounce-stable gesture handling.
- [x] Detect vertical swipe for diagnostics; decide later whether UI uses it.
- [x] Add touch calibration constants/documentation.

## Phase 4 — Firmware structure cleanup

- [x] Split monolithic `src/main.cpp` into modules.
- [x] Add `HaClient` for REST API calls.
- [x] Add `DisplayDriver` wrapper.
- [x] Add `TouchDriver` wrapper.
- [x] Add initial `UiPage` / page system.
- [x] Add `PowerManager` placeholder.
- [x] Keep bring-up/test modes easy to enable with config flags.
- [x] Add serial logging helpers.
- [x] Add build-time firmware version string.

## Phase 5 — Home Assistant API foundation

- [x] Keep `GET /api/` health check.
- [x] Keep `GET /api/states/<entity>` state read.
- [x] Generalize `POST /api/services/<domain>/<service>`.
- [x] Add JSON request body builder helpers.
- [ ] Add error display for HA unavailable/auth failed/entity missing.
- [ ] Add configurable timeout/retry policy.
- [ ] Add HA connection status model.
- [ ] Add optional polling loop for visible state.
- [ ] Defer WebSocket until REST UI is reliable.

## Phase 6 — Safe control tests

- [x] Create HA dummy helpers in `../homeassistant`.
- [x] Test `input_boolean.lily_remote_test` toggle from firmware.
- [x] Test `input_text.lily_remote_last_test` write from firmware.
- [x] Render dummy helper state on e-paper.
- [x] Add on-screen button that toggles only dummy helper.
- [x] Confirm tap -> POST -> state refresh -> UI update flow.
- [ ] Ensure failed POST shows clear UI feedback.

## Phase 7 — UI shell

- [x] Design high-contrast base layout.
- [x] Define initial reusable button component.
- [x] Define status text component.
- [x] Define page title/header component.
- [x] Define bottom/nav area if needed.
- [x] Implement page switching by swipe; physically verified left/right swipes switch pages.
- [x] Implement tap zones/buttons.
- [x] Add full refresh on page changes.
- [ ] Add dirty-region tracking for later partial refresh.
- [x] Create initial placeholder pages:
  - [x] Status
  - [x] Media
  - [x] Lights
  - [x] Activities
  - [x] Info/Debug
- [x] Apply first distinctive grayscale/e-ink visual pass: black rails, inverted labels, status chips, and stronger card hierarchy.
- [x] Add exact-size HTML/CSS UI prototype surface in `docs/ui-prototypes/activities.html` for faster e-ink UX iteration before porting geometry to firmware.
- [x] Use current-page highlighting in bottom nav on every page.
- [x] Add MDI-based icon pipeline for HTML prototype and firmware bitmap icons.
- [x] Port KISSS Home/Activities prototype to firmware: four compact activity cards, top media-off action, status chips above segmented bottom nav.

## Phase 8 — Home Assistant backend contracts

All real actions should be exposed as HA scripts/helpers in `../homeassistant` first.

- [x] Confirm neutral activity scripts exist:
  - [x] `script.activity_watch_tv`
  - [x] `script.activity_watch_plex`
  - [x] `script.activity_play_ps5`
  - [x] `script.activity_stream_music` for streaming music; replaces placeholder `script.activity_music`.
  - [x] `script.activity_listen_records` for phono/records.
  - [x] `script.activity_all_off` for power off; replaces placeholder `script.activity_power_off`.
- [x] Confirm neutral remote helper scripts exist:
  - [x] volume up/down/mute: `script.remote_volume_up`, `script.remote_volume_down`, `script.remote_mute`
  - [x] media play/pause/next/previous: `script.remote_play_pause`, `script.remote_next`, `script.remote_previous`
  - [x] input/source selection: `script.remote_select_hdmi`, `script.remote_select_phono`
- [x] Confirm status summary entities exist:
  - [x] `sensor.remote_summary`
  - [x] TV status via `sensor.remote_summary` attributes.
  - [x] WiiM status/source/volume via `sensor.remote_summary` attributes.
  - [x] KEF power/input status via `sensor.remote_summary` attributes.
  - [x] current activity via `input_select.remote_activity` and `sensor.remote_summary` attributes.
- [x] Add any missing backend helpers in `../homeassistant` via intercom/request: no immediate additions needed; use existing script names above.
- [x] Keep KEF usage limited to power/input assist; WiiM handles volume/source routing through HA scripts.

## Phase 9 — Activity control page

- [x] Render current activity from HA summary on the Activities page.
- [x] Scaffold disabled button layout: Watch TV.
- [x] Scaffold disabled button layout: Play PS5.
- [x] Scaffold disabled button layout: Stream Music.
- [x] Scaffold disabled button layout: Records/Phono.
- [x] Scaffold disabled button layout: Power Off / All Off.
- [x] Move the disabled power-off action out of the main grid into the top-bar media-off button.
- [ ] Enable Button: Watch TV -> `script.activity_watch_tv`.
- [x] Add Button: Watch Plex -> `script.activity_watch_plex`.
- [ ] Enable Button: Play PS5 -> `script.activity_play_ps5`.
- [ ] Enable Button: Stream Music -> `script.activity_stream_music`.
- [ ] Enable Button: Records/Phono -> `script.activity_listen_records`.
- [ ] Enable Button: Power Off -> `script.activity_all_off`.
- [ ] Confirm each enabled button calls only HA scripts.
- [ ] Add confirmation/hold gesture for disruptive actions if needed.
- [ ] Show result/updated activity after call.
- [ ] Handle unavailable TV/KEF/WiiM states gracefully.

## Phase 10 — Media control page

- [x] Create HTML navigation/D-pad prototype with dynamic target selection in `docs/ui-prototypes/navigation.html`.
- [x] Port device-control prototype to firmware Media page: Telia nav, WiiM input/volume, TV input/power, LS60 recovery.
- [x] Update device-control prototype/firmware labels to match deployed HA scripts: TV input+power only, LS60 Coax + unity 71 recovery.
- [ ] Render current media/source summary.
- [ ] Large volume up/down buttons.
- [ ] Mute button.
- [ ] Play/pause button.
- [ ] Previous/next buttons if useful.
- [ ] Source buttons for current real sources:
  - [ ] HDMI
  - [ ] Phono
- [ ] Ensure normal volume/source controls target WiiM through HA, not KEF directly.
- [ ] Add KEF power/input assist only where needed.
- [ ] Add optimistic UI feedback while waiting for HA state refresh.

## Phase 11 — TV / app controls

- [ ] Decide which TV controls are actually needed on e-ink.
- [ ] Add app shortcuts if HA exposes reliable scripts.
- [ ] Add directional/menu controls only if practical on touch/e-ink.
- [ ] Add power state display.
- [ ] Add input display.
- [ ] Avoid trying to replace every physical remote button unless useful.

## Phase 12 — Lights page

- [ ] Confirm HA light groups/scenes in `../homeassistant`.
- [ ] Add scene buttons:
  - [ ] Movie
  - [ ] Evening
  - [ ] Dinner
  - [ ] Bright
  - [ ] Off
- [ ] Add room/group dim up/down.
- [ ] Add color temperature presets only if useful.
- [ ] Show current light scene/brightness if available.
- [ ] Test all actions through HA scripts/scenes first.

## Phase 13 — Info/status page

- [ ] Show WiFi RSSI/IP.
- [ ] Show HA connection status.
- [ ] Show battery level if hardware supports it.
- [ ] Show charging status if hardware supports it.
- [ ] Show time/date from RTC or HA.
- [ ] Show selected HA summary sensors.
- [ ] Add debug page version/build info.

## Phase 14 — Partial refresh

- [ ] Identify library support for partial refresh on ED047TC1.
- [ ] Implement dirty rectangles for small status changes.
- [ ] Partial refresh button press feedback.
- [ ] Partial refresh media/status updates.
- [ ] Full refresh after N partial refreshes to reduce ghosting.
- [ ] Tune refresh regions to avoid artifacts.
- [ ] Document refresh strategy.

## Phase 15 — WebSocket/live state updates

Only do this after REST-driven UI is stable.

- [ ] Implement HA WebSocket auth handshake.
- [ ] Subscribe to relevant state changes.
- [ ] Reconnect on WiFi/HA disconnect.
- [ ] Fall back to REST polling if WebSocket fails.
- [ ] Update UI model from live state events.
- [ ] Keep e-ink refresh rate conservative.

## Phase 16 — Reliability and UX polish

- [ ] Add clear offline screen.
- [ ] Add retry button/action.
- [ ] Add loading/busy indicators suitable for e-ink.
- [ ] Debounce touch events.
- [ ] Prevent duplicate service calls from repeated taps.
- [ ] Add long-press/confirm for power-off or disruptive actions.
- [ ] Make all service call failures visible but not noisy.
- [ ] Add simple serial diagnostics for every action.

## Phase 17 — Power management

- [ ] Add an optional sleep/screensaver screen before idle sleep or low-power idle; not required for e-ink, but useful as a fun/clear "remote is resting" state.
- [ ] Measure active WiFi current draw.
- [ ] Measure idle screen current draw.
- [ ] Measure deep sleep current draw.
- [ ] Measure charging behavior.
- [ ] Confirm battery gauge support.
- [ ] Show battery percentage if reliable.
- [ ] Turn WiFi off when idle if wake/resume remains fast enough.
- [ ] Test light sleep/deep sleep.
- [ ] Confirm touch wake capability.
- [ ] Confirm RTC wake capability.
- [ ] Define idle timeout behavior.
- [ ] Avoid sleep behavior that makes the remote feel unreliable.

## Phase 18 — Enclosure / physical usability

- [ ] Decide portrait vs landscape final orientation.
- [ ] Confirm one-handed usability.
- [ ] Confirm touch targets are large enough.
- [ ] Check glare/contrast in living-room lighting.
- [ ] Check MagSafe/wireless charging behavior.
- [ ] Resolve/document loose internal part issue.
- [ ] Consider case/stand/mount if needed.

## Phase 19 — End-game remote acceptance tests

- [ ] From cold boot, remote shows useful state without serial monitor.
- [ ] Can switch to Watch TV activity.
- [ ] Can switch to PS5 activity.
- [ ] Can switch to Music activity.
- [ ] Can power everything off through HA.
- [ ] Can control WiiM volume/source.
- [ ] Can ensure KEF power/input for WiiM audio path.
- [ ] Can control lights/scenes.
- [ ] Handles TV unavailable gracefully.
- [ ] Handles HA unavailable gracefully.
- [ ] Handles WiFi reconnect gracefully.
- [ ] Does not send duplicate actions on accidental repeated taps.
- [ ] Battery life is acceptable for normal living-room use.
- [ ] No secrets are committed.
- [ ] Home Assistant entities/scripts are documented in `../homeassistant`.
- [ ] Firmware behavior is documented in this repo.

## Later / optional ideas

- [ ] OTA firmware updates.
- [ ] Screenshot-like UI simulator on desktop.
- [ ] Configurable page layout from HA.
- [ ] Icons and 16-grayscale visual polish.
- [ ] Haptic or buzzer feedback if hardware supports it.
- [ ] Multiple room profiles.
- [ ] Weather/calendar page.
- [ ] Find-my-remote helper.
