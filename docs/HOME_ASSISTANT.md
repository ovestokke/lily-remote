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
