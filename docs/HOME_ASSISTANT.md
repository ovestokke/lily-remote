# Home Assistant API Notes

## Long-lived access token

Create a token in Home Assistant:

1. Open Home Assistant.
2. Click user profile.
3. Go to **Security**.
4. Create a **Long-lived access token**.
5. Paste it into local `include/config.h` only.

Do not commit the token.

## Local smoke test with curl

Use shell environment variables so the token is not written to repo files:

```bash
export HA_BASE_URL="http://homeassistant.local:8123"
export HA_TOKEN="paste-token-here"

curl -sS \
  -H "Authorization: Bearer $HA_TOKEN" \
  -H "Content-Type: application/json" \
  "$HA_BASE_URL/api/"
```

Read one entity:

```bash
curl -sS \
  -H "Authorization: Bearer $HA_TOKEN" \
  -H "Content-Type: application/json" \
  "$HA_BASE_URL/api/states/sensor.time"
```

Call a service once you are ready for control actions:

```bash
curl -sS \
  -X POST \
  -H "Authorization: Bearer $HA_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"entity_id":"light.stue"}' \
  "$HA_BASE_URL/api/services/light/toggle"
```

## Firmware API direction

Start with REST:

- `GET /api/` for auth/API reachability.
- `GET /api/states/<entity_id>` for status.
- `POST /api/services/<domain>/<service>` for control actions.

Add WebSocket later when the UI needs live status without polling.

## Real remote backend contract

Real remote buttons must call Home Assistant scripts/helpers, not living-room device entities directly. The current backend lives in the sibling `../homeassistant` repo in `packages/media_remote.yaml`.

### Activity scripts

| Firmware intent | Home Assistant script |
| --- | --- |
| Watch TV | `script.activity_watch_tv` |
| Watch Plex | `script.activity_watch_plex` |
| Play PS5 | `script.activity_play_ps5` |
| Stream music | `script.activity_stream_music` |
| Listen to records / phono | `script.activity_listen_records` |
| Power off / all off | `script.activity_all_off` |

Note: the original firmware placeholder names `script.activity_music` and `script.activity_power_off` do not exist. Use `script.activity_stream_music`, `script.activity_listen_records`, and `script.activity_all_off`.

### Media/source helper scripts

| Firmware intent | Home Assistant script |
| --- | --- |
| Volume up | `script.remote_volume_up` |
| Volume down | `script.remote_volume_down` |
| Mute toggle | `script.remote_mute` |
| Play/pause toggle | `script.remote_play_pause` |
| Next | `script.remote_next` |
| Previous | `script.remote_previous` |
| Select HDMI / TV path | `script.remote_select_hdmi` |
| Select phono | `script.remote_select_phono` |

For high-repeat controls like volume, firmware should send service calls without a full e-paper refresh on every tap. Refresh visible state only after the interaction settles.

### Device-control page contract

The device-control page should use these HA scripts as source of truth:

| Target | Firmware intent | Home Assistant script/data |
| --- | --- | --- |
| Telia | Up/down/left/right/OK/back/home | `script.remote_telia_nav` with `button: up/down/left/right/ok/back/home` |
| WiiM | Select TV input | `script.remote_wiim_select_source` with `source: TV` |
| WiiM | Select phono | `script.remote_wiim_select_source` with `source: Phono In` |
| WiiM | Set absolute volume | `script.remote_wiim_set_volume` with `volume: 0..100` |
| WiiM | Repeated volume/mute | `script.remote_volume_up`, `script.remote_volume_down`, `script.remote_mute` |
| TV | Power | `script.remote_tv_power` with `power_action: on/off/toggle` |
| TV | Select source/app | `script.remote_tv_select_source` with known sources such as `Sagemcom Set-Top Box`, `PS5 Game Console`, `HDMI 4`, `Live TV`, `Plex`, `Netflix` |
| LS60 | Restore unity recovery | `script.remote_ls60_restore_unity_gain` |
| LS60 | Select source | `script.remote_ls60_select_source` with `source: coaxial` |
| LS60 | Set recovery volume | `script.remote_ls60_set_volume` with `volume: 71` |

Device feature split:

- Telia: full navigation.
- WiiM: input and volume; normal preamp path.
- TV: input and power only.
- LS60: input and volume recovery only; normal target is Coax + unity gain volume `71` because WiiM is the external preamp.

### Lights helper script

`script.remote_living_room_lights` accepts a `preset` field. Known presets are:

- `bright`
- `dimmed`
- `relax`
- `nightlight`
- `read`
- `tv`
- `records`
- `off`

### Status entities

- `sensor.remote_summary` is the primary read-only summary state for firmware.
- `input_select.remote_activity` stores current activity; options include `Off`, `Watch TV`, `Watch Plex`, `Play PS5`, `Listen to Records`, `Stream Music`, and `Bathroom Sonos`.
- `input_text.remote_last_message` stores the latest backend message.
- `sensor.remote_summary` attributes include activity, last message, TV, Telia, PS5, WiiM source/volume, KEF power/input, bathroom, and lights state.

## Battery telemetry helpers

Firmware writes latest power/battery event text to `input_text.lily_remote_power_event` and current numeric helpers for voltage, raw SOC, and wake count. Home Assistant exposes parsed template sensors for graphing:

- `sensor.lily_remote_display_soc`
- `sensor.lily_remote_telemetry_raw_soc`
- `sensor.lily_remote_telemetry_voltage`
- `sensor.lily_remote_average_current`
- `sensor.lily_remote_awake`
- `sensor.lily_remote_event_type`
- `sensor.lily_remote_wake_cause`
- `sensor.lily_remote_charge_state`
- `binary_sensor.lily_remote_external_power`

## Safe dummy-control page

The current safe write path uses only the HA helper `input_boolean.lily_remote_test`.
Firmware boots into the dummy-control page when `REMOTE_ENABLE_SAFE_CONTROL_PAGE` is `1`.
Tapping the large on-screen button calls:

```text
POST /api/services/input_boolean/toggle
{"entity_id":"input_boolean.lily_remote_test"}

POST /api/services/input_text/set_value
{"entity_id":"input_text.lily_remote_last_test","value":"dummy toggle -> <state> @ <millis> ms"}
```

After the service call, firmware refreshes the helper state through `GET /api/states/input_boolean.lily_remote_test` and re-renders the page with the new state or an error message. This was verified on-device with HTTP 200 responses for both service calls.
