# Remote UI Implementation Plan

Plan for porting the unified HTML prototype in `docs/ui-prototypes/remote.html` to the LilyGo T5 e-paper remote firmware.

## Phase 1 — Freeze HTML geometry

Before firmware work:

1. Treat `docs/ui-prototypes/remote.html` as the source of truth.
2. Finalize:
   - Header height
   - Bottom nav geometry
   - Home card positions
   - Media device tab positions
   - Telia/WiiM/TV/LS60 subpage layouts
   - Lights zone rows
   - Room status rows
   - More action buttons
3. Avoid changing firmware until the HTML feels good.

Deliverable:

```text
docs/ui-prototypes/remote.html approved enough to port
```

## Phase 2 — Update firmware page model

Move firmware toward this structure:

```cpp
enum class UiPageId {
  Home,
  Media,
  Lights,
  Room,
  More,
  SafeControl,
};

enum class MediaTarget {
  Telia,
  Wiim,
  Tv,
  Ls60,
};
```

State needed:

```cpp
UiPageId currentPage;
MediaTarget currentMediaTarget;
RemoteDisplayStatus status;
String currentActivity;
```

Bottom nav changes page title:

```text
Home -> Media -> Lights -> Room -> More
```

## Phase 3 — Shared layout primitives

Port the common template once:

- `drawTopBar(title)`
  - title left
  - Online chip
  - Off button
- `drawBottomNav(activePage)`
  - Home
  - Media
  - Lights
  - Room
  - More
- Shared button/card helpers:
  - `drawActivityCard(...)`
  - `drawIconButton(...)`
  - `drawSourceButton(...)`
  - `drawZoneRow(...)`
  - `drawStatusRow(...)`
  - `drawPanelTitle(...)`

This avoids each screen becoming custom spaghetti.

## Phase 4 — Icons

Generate/confirm needed MDI bitmap icons.

Already have some icons. Likely additions:

- current marker: `radiobox-marked`
- arrows: up/down/left/right
- volume plus/minus/mute
- skip previous/next
- play-pause
- rewind/fast-forward
- WiFi
- Bluetooth
- speaker
- set-top-box
- HDMI/input
- refresh/back-style icon

Keep using 1-bit BMP arrays in:

```text
src/icons.h
src/icons.cpp
```

## Phase 5 — Render screens

Implement these render functions:

```cpp
void renderHomePage();
void renderMediaPage();
void renderLightsPage();
void renderRoomPage();
void renderMorePage();
```

### Home

Port:

- activity cards
- current activity marker inside active card
- basic controls row:
  - volume down
  - previous
  - play/pause
  - next
  - volume up
- status chips

### Media

Port device tabs:

```text
Telia | WiiM | TV | LS60
```

Sub-renderers:

```cpp
void renderTeliaControls();
void renderWiimControls();
void renderTvControls();
void renderLs60Controls();
```

### Lights

Port:

- Normal / Watch TV / Relax
- All lights on/off
- Hallway on/off
- Kitchen on/off
- Corner lounge on/off
- Dining table on/off
- TV zone on/off

### Room

Port mostly read-only status:

- Activity
- TV
- WiiM
- LS60
- Lights
- Fix LS60
- Refresh

### More

Port:

- All Off
- Refresh Status
- Fix LS60
- WiFi / HA Status
- About Remote
- Sleep Remote
- Safe Test

## Phase 6 — Touch rectangles

For every visible button define explicit rects in `src/ui.h`.

Example:

```cpp
constexpr UiRect kBottomNavHome{...};
constexpr UiRect kMediaTabTelia{...};
constexpr UiRect kTeliaDpadUp{...};
constexpr UiRect kHomeVolumeDown{...};
constexpr UiRect kLightsHallwayOn{...};
```

Then handlers:

```cpp
handleBottomNavTouch();
handleHomeTouch();
handleMediaTouch();
handleLightsTouch();
handleRoomTouch();
handleMoreTouch();
```

## Phase 7 — Action policy before real writes

Do not fire every button the same way.

Add action policy:

```cpp
enum class RemoteActionRefreshPolicy {
  NoRefresh,
  DeferredRefresh,
  FullRefresh,
};

struct RemoteAction {
  const char* service;
  const char* payload;
  RemoteActionRefreshPolicy refreshPolicy;
  bool requiresConfirm;
};
```

Suggested behavior:

- Volume, nav, play/pause:
  - send command
  - no full refresh
- Source/activity/scene:
  - send command
  - deferred refresh after short delay
- All Off:
  - require confirm
  - full refresh after action
- Refresh:
  - full refresh
- Page switches:
  - full refresh

## Phase 8 — Wire Home Assistant scripts

Use scripts only, not raw devices.

### Home basic controls

```text
script.remote_volume_down
script.remote_previous
script.remote_play_pause
script.remote_next
script.remote_volume_up
```

### Activities

```text
script.activity_watch_tv
script.activity_play_ps5
script.activity_stream_music
script.activity_listen_records
script.activity_all_off
```

### Telia

```text
script.remote_telia_nav { button: up/down/left/right/ok/back/home }
script.remote_telia_command { command: MEDIA_PLAY_PAUSE / MEDIA_REWIND / MEDIA_FAST_FORWARD }
script.remote_telia_launch_app { app: plex/youtube/spotify }
```

Spotify script/package still needs HA confirmation/implementation.

### WiiM

```text
script.remote_volume_up
script.remote_volume_down
script.remote_mute
script.remote_play_pause
script.remote_previous
script.remote_next
script.remote_wiim_select_hdmi
script.remote_wiim_select_phono
script.remote_wiim_select_aux
script.remote_wiim_select_wifi
```

### TV

```text
script.remote_tv_power { power_action: on/toggle/off }
script.remote_tv_select_source { source: Sagemcom Set-Top Box / PS5 Game Console / HDMI 4 / Live TV }
```

### LS60

```text
script.remote_ls60_restore_unity_gain
script.remote_ls60_select_coaxial
script.remote_ls60_set_volume { volume: 71 }
script.remote_ls60_select_analog
script.remote_ls60_select_optical
script.remote_ls60_select_tv
script.remote_ls60_select_bluetooth
```

### Lights

Need HA scripts created first:

```text
script.remote_light_zone_on { zone: ... }
script.remote_light_zone_off { zone: ... }
script.remote_light_scene { scene: normal/watch_tv/relax }
```

## Phase 9 — Enable writes gradually

Enable in this order:

1. Refresh/status only
2. Telia nav
3. WiiM volume/playback
4. WiiM source buttons
5. LS60 restore unity
6. TV source/power
7. Lights
8. Activities
9. All Off last, with confirmation

## Phase 10 — Test on device

For each page:

- Boot renders Home
- Bottom nav changes pages
- Media device tabs switch correctly
- No accidental double actions
- Volume/nav actions do not full-refresh every tap
- Refresh/status updates still work
- E-paper readability verified physically

Final acceptance:

```text
HTML design and firmware screen match closely enough.
Touch targets feel usable.
No dangerous action fires without intended policy.
```
