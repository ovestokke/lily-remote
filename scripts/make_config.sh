#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
config="${repo_root}/include/config.h"
example="${repo_root}/include/config.example.h"

if [[ -e "${config}" ]]; then
  echo "Already exists: ${config}"
  exit 0
fi

cp "${example}" "${config}"
echo "Created ${config}"
echo "Edit it with your WiFi and Home Assistant settings. Do not commit it."
