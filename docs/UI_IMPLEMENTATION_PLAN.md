# Lily Remote — UI Implementation Plan (v2)

Based on `docs/ui-prototypes/remote.html`. HTML is the single source of truth for geometry, style, and interaction model. Existing firmware is the starting point.

**Firmware already has:**
- Touch calibration: GT911 → screen coord identity mapping works
- Icon bitmaps: 22×22 (nav), 23×23 (device tabs), 28×28 (side buttons), 42×42 (D-pad), 48×48 (volume), 50×50 (activity cards)
- Display helpers: `drawText()`, `drawMdiIcon()`, `drawHLine()`, `drawLabelPill()`, `drawQuickStatusChip()`, `drawFooterNav()`, `drawDeviceTabs()`, D-pad, OK key, grid button helpers
- `HaClient`: `getEntityState()`, `callService()`, `callEntityService()`, `callEntityServiceWithStringField()`
- `app.cpp`: page switching (swipe + nav), per-page touch handlers, `RemoteDisplayStatus`, `RemoteDeviceTarget`
- Working pages: `renderActivitiesPage()`, `renderDeviceControlPage()`, `renderSafeControlPage()`, `renderShellPage()`, `renderStatusPage()`, `renderTouchTestPage()`

**Key gaps to match the HTML:**
1. Page model doesn't match HTML page names/structure
2. Top bar is inconsistent: HTML has title+online pill+Off button; firmware has brand+online pill+off button
3. No Home quick controls row (vol-, prev, play/pause, next, vol+)
4. No current-activity badge on the active activity card
5. No Lights page, Room page, or More page real renderers
6. No HA script wiring (actions are log-only or SafeControl only)
7. No state polling from HA
8. Device control panels differ from HTML layout (Telia missing transport row + app launchers, WiiM missing source grid, TV missing power toggle row, LS60 missing full source grid)
9. No action policy / debounce / refresh strategy
10. No error/offline states, no sleep, no about page

---

## Screen coordinate system

```
Display: 540w × 960h, portrait, USB down
Frame padding: 0 24px (left/right)
Content area: 492w × 960h

Top bar: 112px, 2px bottom border (y 0–114)
Footer: 112px (y 848–960), bottom nav centered: y 856–914, 58px tall
Content: y 114–848 (734px usable height)
```

All UiRect values come from HTML CSS px values. Verify on device after each phase.

---

## Phase 1 — Update page model to match HTML

### 1.1 Update `UiPageId` in `src/ui.h`

Replace the enum. The existing `UiPageId::Activities` becomes the new `Home`. Add missing pages.

```cpp
enum class UiPageId : uint8_t {
  Home,    // ≡ current Activities page, now with quick controls + chips
  Media,   // device tabs + panels (unchanged)
  Lights,  // scenes + zone rows (new real renderer)
  Room,    // status list + recovery (new real renderer)
  More,    // All Off / Refresh / Fix LS60 / Status / About / Sleep / Safe Test
  SafeControl, // development: toggle helper entity (keep)
  TouchTest,   // development: calibration (keep)
};
```

### 1.2 Update `uiPageName()` in `src/ui.cpp`

Return `{ "home", "media", "lights", "room", "more", "safe_control", "touch_test" }`.

### 1.3 Update bottom nav rect labels in `src/ui.h`

Match HTML nav order:

```cpp
constexpr UiRect kNavHome   {40,  856, 92, 58};  // was kBottomNavHomeButton
constexpr UiRect kNavMedia {132, 856, 92, 58};
constexpr UiRect kNavLights{224, 856, 92, 58};
constexpr UiRect kNavRoom  {316, 856, 92, 58};
constexpr UiRect kNavMore  {408, 856, 92, 58};
```

Update `app.cpp`: rename `kBottomNav*Button` → `kNav*` throughout, and `kNavInfoButton` → `kNavRoom`. The "Info" nav button should map to `UiPageId::Room`.

### 1.4 Update `app.cpp` page switching

- `g_currentPage` initial value: `UiPageId::Home`
- `nextPage()` / `previousPage()` order: Home → Media → Lights → Room → More → Home
- Bottom nav maps to new page IDs
- The `makeShellPage()` switch case for `UiPageId::Info` becomes `UiPageId::Room`

---

## Phase 2 — Unified top bar

Replace `drawKisssTopBar()` with `drawTopBar(title, isOnline, showOffButton)` in `src/display.cpp`. This becomes the shared layout primitive for every page.

**Geometry from HTML:**
- Left: nothing or page-specific content (page title replaces brand block)
- Right: Online pill (38px tall, 2px border, uppercase, right side) + Off button (38px tall, `--ink` background, icon + "Off" label, next to pill)
- Title text: 44px Courier Prime, `--ink`, left-aligned after frame margin

For the Home page title, use "Home". For Media, Lights, Room, More, use the page name. The brand block ("LILY REMOTE / LIVING ROOM") is removed.

**Online pill:** if `isOnline` true: border, normal text "ONLINE"; if false: solid ink background, white text "OFF".

**Off button:** fires `script.activity_all_off` on tap. Keep the 3-second confirmation overlay pattern from Phase 8.

```cpp
void drawTopBar(const char* title, bool isOnline) {
  // title: 44px font, left at kFrameLeft+4, y = kTopBarHeight - 36
  // online pill: right-aligned, 38px tall, 2px border, "ONLINE"/"OFF"
  // off button: next to pill, 38px tall, ink fill, power icon + "Off" text
  // bottom border: 2px line at y = kTopBarHeight
}
```

**Deliverable:** `drawTopBar(title, isOnline)` replaces `drawKisssTopBar()` and the brand block in every page renderer. Add `kFrameLeft = 24` and `kTopBarHeight = 112` as module constants.

---

## Phase 3 — Activity cards with current badge (Home page)

### 3.1 Update `renderActivitiesPage()` → `renderHomePage()`

Rename the renderer and update the page struct. Add `currentActivity` field that was already being extracted from `sensor.remote_summary`.

**From HTML:**
- Activity card grid: 2 columns, 220w × 126h each, gap 14px, gap 16px between cols
  - Watch TV: `x=40, y=238`
  - Play PS5: `x=280, y=238`
  - Stream: `x=40, y=378`
  - Records: `x=280, y=378`
- Each card: 3px border, 12px left stripe (ink fill), icon (50×50) top-left at (24,18), title text bottom-left at (24, y+88)
- **Current badge:** if card matches `currentActivity`, draw ink-filled card with white text and `mdi-radiobox-marked` 28×28 at top-right (x = card.x + card.w - 44, y = card.y + 20)

### 3.2 Quick controls row

From HTML `.quick-controls`:
- y = 746, height 76, full width from x=24 to x=492
- 5 buttons in a 1fr 1fr 1.24fr 1fr 1fr grid, 2px border around row
- Each button: border-right 2px (ink), icon centered
- `Vol-`: `mdi-volume-minus` 32×32
- `Prev`: `mdi-skip-previous` 32×32
- `Play/Pause`: `mdi-play-pause` 38×38, **ink fill** (`.primary` class in HTML)
- `Next`: `mdi-skip-next` 32×32
- `Vol+`: `mdi-volume-plus` 32×32

### 3.3 Status chips row

From HTML `.quick-status`:
- y = 686, height 50, 3 chips (1fr 1fr 1fr), gap 10px
- Chip: 2px border, `display:grid; place-items:center`, text centered
- Values from `RemoteState`: TV state, WiiM state + source, LS60 source

### 3.4 Touch rects for Home page

Add to `src/ui.h`:

```cpp
constexpr UiRect kActivityWatchTv{40,  238, 220, 126};
constexpr UiRect kActivityPs5    {280, 238, 220, 126};
constexpr UiRect kActivityStream {40,  378, 220, 126};
constexpr UiRect kActivityRecords{280, 378, 220, 126};

constexpr UiRect kQuickVolDown{24,   746, 82, 76};
constexpr UiRect kQuickPrev  {106,  746, 82, 76};
constexpr UiRect kQuickPlay  {188,  746, 102, 76};
constexpr UiRect kQuickNext {290,  746, 82, 76};
constexpr UiRect kQuickVolUp {372,  746, 82, 76};

constexpr UiRect kChipTV  {24,  686, 148, 50};
constexpr UiRect kChipWiiM{180, 686, 148, 50};
constexpr UiRect kChipKEF {336, 686, 148, 50};
```

---

## Phase 4 — Media page rebuild

The current `renderDeviceControlPage()` uses a 2×2 grid for all device panels. The HTML uses different layouts per device. Replace per-device panel renderers.

### 4.1 Telia panel

From HTML `#panel-telia`:
- Panel title bar: 54px tall, ink fill, white text "Telia" + "navigation"
- D-pad: 3×3 grid in a `dpad` div, total 318px tall
  - Grid uses 2px borders between cells, 3px outer border, paper-deep background
  - Up/Left/Right/Down each 112×112
  - OK: 112×112, ink fill, white text "OK"
  - Empty cells: paper-deep fill, no content
- Side stack: 194px wide, 318px tall, two 112px buttons "Back" and "Home" with 12px gap, stacked vertically
- Transport row: 3 equal buttons, 64px tall, 2px border, icons: rewind, play-pause, fast-forward (30×30)
- App launcher row: 3 equal buttons, ~60px tall, text labels "Plex", "YouTube", "Spotify"

**Touch rects for Telia panel:**

```cpp
constexpr UiRect kTeliaBack{40, 470, 194, 112};
constexpr UiRect kTeliaHome{40, 582, 194, 112};
constexpr UiRect kTeliaUp   {234, 470, 112, 112};
constexpr UiRect kTeliaLeft {234, 582, 112, 112};
constexpr UiRect kTeliaOK    {346, 582, 112, 112};
constexpr UiRect kTeliaRight{458, 582, 112, 112};
constexpr UiRect kTeliaDown {234, 694, 112, 112};
constexpr UiRect kTeliaRewind{40,  820, 164, 64};
constexpr UiRect kTeliaPlay  {204, 820, 164, 64};
constexpr UiRect kTeliaFFwd  {368, 820, 164, 64};
constexpr UiRect kTeliaPlex   {40,  896, 164, 60};
constexpr UiRect kTeliaYouTube{204, 896, 164, 60};
constexpr UiRect kTeliaSpotify{368, 896, 164, 60};
```

### 4.2 WiiM panel

From HTML `#panel-wiim`:
- Panel title: "WiiM" + "preamp"
- Volume grid: 3 buttons, 112px tall, each with 42×42 icon
  - Vol- (left), Mute (center, ink fill), Vol+ (right)
- Source grid: 2 columns, 92px tall each, gap 12px
  - HDMI (TV input), Phono (records), Aux (line in), Wi-Fi (network)
  - Each: 48px wide icon + label (text-transform none), source grid total width = 150+12+150 = 312px; centers in content area
- Transport row: Prev / Play / Next, 64px tall

**Touch rects:**

```cpp
constexpr UiRect kWiimVolDown{40,  168, 150, 112};
constexpr UiRect kWiimMute   {190, 168, 150, 112};
constexpr UiRect kWiimVolUp  {340, 168, 150, 112};

constexpr UiRect kWiimHDMI {40,  280, 150, 92};
constexpr UiRect kWiimPhono {190, 280, 150, 92};
constexpr UiRect kWiimAux  {40,  372, 150, 92};
constexpr UiRect kWiimWiFi {190, 372, 150, 92};

constexpr UiRect kWiimPrev {40,  464, 150, 64};
constexpr UiRect kWiimPlay {190, 464, 150, 64};
constexpr UiRect kWiimNext {340, 464, 150, 64};
```

### 4.3 TV panel

From HTML `#panel-tv`:
- Title: "TV" + "power + input"
- Power row: 3 buttons (On / Toggle / Off), 78px tall, gap 12px
  - Toggle has ink fill (`.primary`)
- Source grid: 2×2, 92px tall each
  - Telia (Sagemcom), PS5 (console), HDMI 4 (spare), Live TV (fallback)

**Touch rects:**

```cpp
constexpr UiRect kTVPowerOn    {40,  168, 150, 78};
constexpr UiRect kTVPowerToggle{190, 168, 150, 78};
constexpr UiRect kTVPowerOff   {340, 168, 150, 78};

constexpr UiRect kTVSourceTelia {40,  246, 150, 92};
constexpr UiRect kTVSourcePS5    {190, 246, 150, 92};
constexpr UiRect kTVSourceHDMI4  {40,  338, 150, 92};
constexpr UiRect kTVSourceLive   {190, 338, 150, 92};
```

### 4.4 LS60 panel

From HTML `#panel-ls60`:
- Title: "LS60" + "recovery"
- Wide button: 460w × 106h, "Restore unity", ink fill, icon + label + "71" text right-aligned
- Source grid: 3 columns, 2 rows, 92px tall
  - Coax, Vol 71, Analog, Optical, TV, Bluetooth

**Touch rects:**

```cpp
constexpr UiRect kLS60RestoreUnity{40, 168, 460, 106};
constexpr UiRect kLS60Coax   {40,  274, 150, 92};
constexpr UiRect kLS60Vol71  {190, 274, 150, 92};
constexpr UiRect kLS60Analog {340, 274, 150, 92};
constexpr UiRect kLS60Optical{40,  366, 150, 92};
constexpr UiRect kLS60TV    {190, 366, 150, 92};
constexpr UiRect kLS60Bluetooth{340, 366, 150, 92};
```

---

## Phase 5 — Lights page real renderer

From HTML `#page-lights`:
- Scene buttons: 3 columns, 82px tall, gap 10px, y=134
  - Normal (ink fill), Watch TV, Relax
- Zone list: 6 rows, each 62px tall, gap 10px, y=232
  - All lights (72px tall with margin-top 2px)
  - Hallway, Kitchen, Corner lounge, Dining table, TV zone
- Each row: 3-column grid — name (flex), On button (72px), Off button (72px)
  - On button has ink fill in HTML, Off is border-only

**Touch rects:**

```cpp
constexpr UiRect kSceneNormal  {40,  134, 150, 82};
constexpr UiRect kSceneWatchTV {190, 134, 150, 82};
constexpr UiRect kSceneRelax   {340, 134, 150, 82};

constexpr UiRect kZoneAll    {40, 232, 460, 72};
constexpr UiRect kZoneAllOn  {332, 232, 72, 72};
constexpr UiRect kZoneAllOff {404, 232, 72, 72};

constexpr UiRect kZoneHallway  {40, 314, 460, 62};
constexpr UiRect kZoneKitchen  {40, 386, 460, 62};
constexpr UiRect kZoneCorner   {40, 458, 460, 62};
constexpr UiRect kZoneDining   {40, 530, 460, 62};
constexpr UiRect kZoneTV       {40, 602, 460, 62};

// On/Off buttons within each row (zone x + zone w - 144)
constexpr UiRect kZoneHallwayOn  {388, 314, 72, 62};
constexpr UiRect kZoneHallwayOff {460, 314, 72, 62};
// same pattern for Kitchen, Corner, Dining, TV
```

**Zone on/off:** `media_remote.yaml` only has `remote_living_room_lights` with presets. Individual zone scripts do not exist. For Phase 5, implement the UI and wire zone buttons to `light.turn_on` / `light.turn_off` via `HaClient::callEntityService()` directly (e.g., `light.turn_on` with `entity_id: light.hallway`). Document this as a temporary solution until zone scripts are added to HA. Add a note in `docs/ACTION_POLICY.md` that zone scripts should be preferred.

---

## Phase 6 — Room page real renderer

From HTML `#page-room`:
- Status list: 5 rows, 68px tall, gap 10px, y=134
  - Activity, TV (with source), WiiM (with source + volume), LS60 (with source + volume), Lights
- Each row: 104px left column (label, uppercase, 2px right border) + right column (value + optional small subtitle)
- Recovery actions: 2×2 grid, 82px tall, gap 12px, below status rows
  - Fix LS60 (primary, ink fill), Refresh (secondary)

**Touch rects:**

```cpp
constexpr UiRect kStatusActivity{40, 134, 460, 68};
constexpr UiRect kStatusTV     {40, 212, 460, 68};
constexpr UiRect kStatusWiiM   {40, 290, 460, 68};
constexpr UiRect kStatusLS60   {40, 368, 460, 68};
constexpr UiRect kStatusLights {40, 446, 460, 68};

constexpr UiRect kRoomFixLS60 {40,  530, 218, 82};
constexpr UiRect kRoomRefresh {250, 530, 218, 82};
```

---

## Phase 7 — More page real renderer

From HTML `#page-more`:
- 2-column grid of action buttons, 82px tall, gap 12px
- All Off: wide button (spans 2 columns), ink fill, icon + "All Off"
- 6 secondary buttons in 2×3 grid
- "Safe Test" button: calls SafeControl page toggle

**Touch rects:**

```cpp
constexpr UiRect kMoreAllOff  {40, 134, 460, 82};
constexpr UiRect kMoreRefresh {40,  228, 218, 82};
constexpr UiRect kMoreFixLS60{250, 228, 218, 82};
constexpr UiRect kMoreWiFi   {40,  322, 218, 82};
constexpr UiRect kMoreAbout  {250, 322, 218, 82};
constexpr UiRect kMoreSleep  {40,  416, 218, 82};
constexpr UiRect kMoreSafe   {250, 416, 218, 82};
```

**About Remote:** display `REMOTE_FIRMWARE_NAME` + `REMOTE_FIRMWARE_VERSION` from `src/version.h`, WiFi RSSI, HA connection status. No HA call.

**Sleep Remote:** call `g_powerManager.goToSleep()` — implement `goToSleep()` in `PowerManager` (currently a stub). This requires deep sleep setup in `platformio.ini` / board config. If deep sleep is not yet wired, show "Coming soon" and log a warning.

---

## Phase 8 — HA script wiring

All actions fire `HaClient::callService("script", "turn_on")` with YAML variables in the POST body.

### 8.1 Script call helper

```cpp
bool callScript(const char* scriptName, const String& variables = "{}") {
  // POST /api/services/script.turn_on with {"entity_id": "script.<name>", "variables": <variables>}
  String body = "{\"entity_id\":\"script.";
  body += scriptName;
  body += "\"";
  if (variables.length() > 0) {
    body += ",\"variables\":";
    body += variables;
  }
  body += "}";
  return g_haClient.postJson("/api/services/script/turn_on", body);
}
```

### 8.2 Action → script mapping

| Tap location | Script | Variables |
|---|---|---|
| Home: Watch TV card | `activity_watch_tv` | — |
| Home: Play PS5 card | `activity_play_ps5` | — |
| Home: Stream card | `activity_stream_music` | — |
| Home: Records card | `activity_listen_records` | — |
| Home: Vol Down | `remote_volume_down` | — |
| Home: Prev | `remote_previous` | — |
| Home: Play/Pause | `remote_play_pause` | — |
| Home: Next | `remote_next` | — |
| Home: Vol Up | `remote_volume_up` | — |
| Home: Off button | `activity_all_off` | — |
| Media/Telia: Up | `remote_telia_nav` | `{"button":"up"}` |
| Media/Telia: Left | `remote_telia_nav` | `{"button":"left"}` |
| Media/Telia: OK | `remote_telia_nav` | `{"button":"ok"}` |
| Media/Telia: Right | `remote_telia_nav` | `{"button":"right"}` |
| Media/Telia: Down | `remote_telia_nav` | `{"button":"down"}` |
| Media/Telia: Back | `remote_telia_nav` | `{"button":"back"}` |
| Media/Telia: Home | `remote_telia_nav` | `{"button":"home"}` |
| Media/Telia: Rewind | `remote_telia_command` | `{"command":"MEDIA_REWIND"}` |
| Media/Telia: Play | `remote_telia_command` | `{"command":"MEDIA_PLAY_PAUSE"}` |
| Media/Telia: FFwd | `remote_telia_command` | `{"command":"MEDIA_FAST_FORWARD"}` |
| Media/Telia: Plex | `remote_telia_launch_plex` | — |
| Media/Telia: YouTube | `remote_telia_launch_youtube` | — |
| Media/Telia: Spotify | `remote_telia_launch_spotify` | — |
| Media/WiiM: Vol- | `remote_volume_down` | — |
| Media/WiiM: Mute | `remote_mute` | — |
| Media/WiiM: Vol+ | `remote_volume_up` | — |
| Media/WiiM: HDMI | `remote_wiim_select_hdmi` | — |
| Media/WiiM: Phono | `remote_wiim_select_phono` | — |
| Media/WiiM: Aux | `remote_wiim_select_aux` | — |
| Media/WiiM: Wi-Fi | `remote_wiim_select_wifi` | — |
| Media/WiiM: Prev | `remote_previous` | — |
| Media/WiiM: Play | `remote_play_pause` | — |
| Media/WiiM: Next | `remote_next` | — |
| Media/TV: Power On | `remote_tv_power` | `{"power_action":"on"}` |
| Media/TV: Power Toggle | `remote_tv_power` | `{"power_action":"toggle"}` |
| Media/TV: Power Off | `remote_tv_power` | `{"power_action":"off"}` |
| Media/TV: Telia source | `remote_tv_select_source` | `{"source":"Sagemcom Set-Top Box"}` |
| Media/TV: PS5 source | `remote_tv_select_source` | `{"source":"PS5 Game Console"}` |
| Media/TV: HDMI 4 source | `remote_tv_select_source` | `{"source":"HDMI 4"}` |
| Media/TV: Live TV source | `remote_tv_select_source` | `{"source":"Live TV"}` |
| Media/LS60: Restore unity | `remote_ls60_restore_unity_gain` | — |
| Media/LS60: Coax | `remote_ls60_select_coaxial` | — |
| Media/LS60: Vol 71 | `remote_ls60_set_volume` | `{"volume":71}` |
| Media/LS60: Analog | `remote_ls60_select_analog` | — |
| Media/LS60: Optical | `remote_ls60_select_optical` | — |
| Media/LS60: TV | `remote_ls60_select_tv` | — |
| Media/LS60: Bluetooth | `remote_ls60_select_bluetooth` | — |
| Lights: Normal | `remote_living_room_lights` | `{"preset":"bright"}` |
| Lights: Watch TV | `remote_living_room_lights` | `{"preset":"tv"}` |
| Lights: Relax | `remote_living_room_lights` | `{"preset":"relax"}` |
| Lights: All On | `remote_living_room_lights` | `{"preset":"bright"}` |
| Lights: All Off | `remote_living_room_lights` | `{"preset":"off"}` |
| Lights: zone On/Off | `light.turn_on/turn_off` | direct entity (see Phase 5 note) |
| Room: Fix LS60 | `remote_ls60_restore_unity_gain` | — |
| Room: Refresh | `remote_status_refresh` | — |
| More: All Off | `activity_all_off` | — |
| More: Refresh Status | `remote_status_refresh` | — |
| More: Fix LS60 | `remote_ls60_restore_unity_gain` | — |
| More: About | (no HA call) | — |
| More: Sleep | (power manager) | — |
| More: Safe Test | (switch to SafeControl page) | — |

### 8.3 `HaClient` JSON body for script variables

The HA script turn_on endpoint accepts `variables` in the JSON body. Add a new `HaClient` method:

```cpp
bool callScript(const char* scriptName, const String& variables = "{}") {
  String body = "{\"entity_id\":\"script.";
  body += scriptName;
  body += "\"";
  if (variables.length() > 0) {
    body += ",\"variables\":";
    body += variables;
  }
  body += "}";
  return postJson("/api/services/script/turn_on", body);
}
```

---

## Phase 9 — State polling

### 9.1 Poll targets

Read `sensor.remote_summary` on every render and every 30 seconds via `HaClient::getEntityState()`. Parse from the `|`-delimited string:
```
input_select.remote_activity | media_player.lg_oled55c25lb | media_player.lelia_box_2 | sensor.ps5_now_playing | media_player.living_room_ultra_4 | media_player.kef_ls60
```

Also poll individual entities for dynamic state:
- `media_player.living_room_ultra_4`: source + volume_level (for WiiM panel state)
- `media_player.kef_ls60`: source + volume_level (for LS60 panel state)
- `media_player.lg_oled55c25lb`: state + source (for TV panel state)
- `light.stue`, `light.tv_zone`: on/off (for Lights zone state)

### 9.2 `RemoteState` struct

Add to `src/display.h`:

```cpp
struct RemoteState {
  bool haOnline;
  String activity;       // from sensor.remote_summary attr:activity
  String tvState;        // on/off/unavailable
  String tvSource;
  String wiimState;
  String wiimSource;
  float wiimVolume;      // 0.0-1.0
  String ls60State;
  String ls60Source;
  float ls60Volume;
  String lastMessage;
  bool lightsStue;
  bool lightsTVZone;
};
```

### 9.3 State polling function

```cpp
RemoteState pollRemoteState(HaClient& ha) {
  RemoteState s;
  s.haOnline = ha.getApiMessage(s.lastMessage);
  String summary;
  if (ha.getEntityState("sensor.remote_summary", summary)) {
    // Parse: activity | tv | telia | ps5 | wiim | kef
    int p1 = summary.indexOf('|');
    s.activity = summary.substring(0, p1);
    // ... parse remaining fields
  }
  // poll individual entities...
  return s;
}
```

Run `pollRemoteState()` at boot, on explicit Refresh button tap, and every 30 seconds in `loopRemoteApp()`.

### 9.4 Refresh policy

| Action | E-ink refresh |
|---|---|
| Page switch | Full |
| Activity card tap | Full |
| Volume / transport / D-pad tap | Partial (just update status chips) |
| Source / scene / power tap | Deferred full (300ms delay, then full) |
| All Off | Full + 3s confirm overlay |
| Refresh tap | Full |
| Periodic 30s poll | Partial |

Implement partial refresh: `g_epaper.partialUpdate()` instead of `fullUpdate()`. This requires verifying `FastEPD::partialUpdate()` works on EPDiy V7. If not, fall back to full refresh but add a note.

---

## Phase 10 — Action policy and debounce

### 10.1 Debounce

Track `g_lastActionMs` per action type. Ignore duplicate taps within 200ms:

```cpp
static uint32_t s_lastActionMs = 0;
if (millis() - s_lastActionMs < 200) return;
s_lastActionMs = millis();
```

### 10.2 All Off confirmation overlay

Draw a 2-second overlay on the current page: "All Off? Tap again." with a countdown indicator. If user taps again within 2 seconds, fire the script and dismiss overlay. If 2 seconds pass, dismiss without firing.

Implement as a boolean flag `g_pendingAllOffConfirm` in `app.cpp`. In the overlay, disable all other buttons except the confirm tap zone.

### 10.3 Action feedback

On successful script call: update status chips via partial refresh (no full refresh). On failure: log error, optionally update `lastMessage` and do a full refresh after 500ms.

---

## Phase 11 — Error and offline states

### 11.1 HA unreachable

`drawTopBar()` shows "OFF" pill instead of "ONLINE". All buttons remain visible but fire calls that will fail. On call failure, call `pollRemoteState()` to confirm HA status and update the pill.

### 11.2 Per-entity unavailable

For unavailable device states in status chips: show "???" in the chip. Device panel tabs for unavailable devices remain visible. Source buttons that target unavailable entities: show a brief error text on the status row (e.g., "TV unavailable").

### 11.3 About page

Display: firmware name + version from `src/version.h`, WiFi RSSI, HA connection URL (masked: show host only), build date.

---

## Phase 12 — On-device verification checklist

- [ ] Home: activity cards highlight current activity, quick controls respond, status chips update
- [ ] Media: device tabs switch panels, Telia D-pad + transport + launchers all fire correct scripts
- [ ] Media: WiiM source buttons, volume, playback control work
- [ ] Media: TV power/source buttons work
- [ ] Media: LS60 restore unity + source buttons work
- [ ] Lights: scene buttons fire `remote_living_room_lights`, zone buttons fire correct entities
- [ ] Room: status reflects actual HA state after refresh
- [ ] More: All Off shows confirm overlay, Safe Test navigates to SafeControl
- [ ] More: Sleep enters deep sleep (or shows "Coming soon")
- [ ] Bottom nav: switches pages, correct title in top bar
- [ ] E-ink: full refresh on page switch, partial refresh on quick controls
- [ ] HA offline: "OFF" pill shown, buttons still visible, no crash
- [ ] Touch: no accidental double-fires (debounce working)

---

## File ownership summary

| File | Changes |
|---|---|
| `src/ui.h` | Update `UiPageId`, rename nav rects, add all new touch rects from Phases 3–7 |
| `src/ui.cpp` | Update `uiPageName()`, rename nav rect references |
| `src/display.h` | Add `RemoteState` struct, `drawTopBar()` declaration |
| `src/display.cpp` | Implement `drawTopBar()`, rename `renderActivitiesPage()` → `renderHomePage()`, rebuild Telia/WiiM/TV/LS60 panel renderers, add `renderLightsPage()`, `renderRoomPage()`, `renderMorePage()`, `renderAboutPage()` |
| `src/ha_client.h` | Add `callScript(script, variables)` method |
| `src/ha_client.cpp` | Implement `callScript()` |
| `src/app.cpp` | Update page IDs, `makeShellPage()`, `nextPage()`/`previousPage()`, touch dispatch for Home/Media/Lights/Room/More, state polling in `loopRemoteApp()`, HA script calls, action policy, debounce, All Off confirm |
| `src/power_manager.h` | Add `goToSleep()` declaration |
| `src/power_manager.cpp` | Implement `goToSleep()` using ESP32 deep sleep |
| `include/config.h` | Add zone entity IDs for lights, optional partial refresh enable flag |

---

## Execution order

```
Phase 1  Page model + nav rect rename
Phase 2  Unified drawTopBar() primitive
Phase 3  Home page renderer (activity cards + quick controls + chips)
Phase 4  Media page rebuild (per-device panels + all new touch rects)
Phase 5  Lights page real renderer + zone entity wiring
Phase 6  Room page real renderer
Phase 7  More page real renderer
Phase 8  HA script call helper + all script wiring
Phase 9  State polling + RemoteState struct
Phase 10 Action policy (debounce, confirm, refresh strategy)
Phase 11 Error/offline states + About page
Phase 12 On-device verification
```

Phase 1–2 are structural foundation. Phase 3–7 build each page's renderer. Phase 8 wires actions. Phase 9–10 add state and policy. Phase 11 adds polish. Phase 12 is the test pass.