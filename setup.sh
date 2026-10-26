#!/bin/sh
set -eu
cd "$(dirname "$0")"
command -v docker >/dev/null 2>&1 || { echo 'Install Docker with Compose, then rerun setup.' >&2; exit 1; }
docker compose version >/dev/null
docker compose build bios
printf '%s\n' 'Ready. Run: docker compose up bios' 'Console: http://localhost:6080/vnc.html?autoconnect=true&resize=scale'
