#!/usr/bin/env bash
set -euo pipefail

: "${HA_BASE_URL:?Set HA_BASE_URL, e.g. http://homeassistant.local:8123}"
: "${HA_TOKEN:?Set HA_TOKEN to a Home Assistant long-lived access token}"
ENTITY_ID="${1:-sensor.time}"

base="${HA_BASE_URL%/}"

echo "== Home Assistant API =="
curl -sS \
  -H "Authorization: Bearer ${HA_TOKEN}" \
  -H "Accept: application/json" \
  "${base}/api/"

echo
echo "== Entity: ${ENTITY_ID} =="
curl -sS \
  -H "Authorization: Bearer ${HA_TOKEN}" \
  -H "Accept: application/json" \
  "${base}/api/states/${ENTITY_ID}"

echo
