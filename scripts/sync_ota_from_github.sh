#!/usr/bin/env bash
set -euo pipefail

# Example:
#   GITHUB_TOKEN=<github-token> ./scripts/sync_ota_from_github.sh dev artifact
#   ./scripts/sync_ota_from_github.sh stable release

CHANNEL="${1:-stable}"
SOURCE="${2:-release}"
REPO="${GITHUB_REPO:-zuo-qirun/Amap-for-ESP32}"
WEB_ROOT="${OTA_WEB_ROOT:-/www/wwwroot/ota.zuoqirun.top/ota}"

python3 "$(dirname "$0")/sync_ota_from_github.py" \
  --repo "$REPO" \
  --channel "$CHANNEL" \
  --source "$SOURCE" \
  --web-root "$WEB_ROOT"
