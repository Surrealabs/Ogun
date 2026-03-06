#!/usr/bin/env bash
# ============================================================
#  rover-auto-update.sh
#  Periodic unattended updater for the rover software stack.
#
#  Designed to run from a systemd timer as root.
#  It updates rover_server from GitHub Releases without
#  flashing the Teensy firmware.
# ============================================================
set -euo pipefail

LOCK_FILE="/var/lock/rover-auto-update.lock"
LOG_FILE="/var/log/rover-auto-update.log"

mkdir -p "$(dirname "$LOCK_FILE")" "$(dirname "$LOG_FILE")"

# Single-run lock to prevent overlapping timer invocations.
exec 9>"$LOCK_FILE"
if ! flock -n 9; then
    exit 0
fi

# Resolve ogun CLI location.
OGUN_BIN=""
for cand in /usr/local/bin/ogun /opt/rover-src/ogun /opt/rover/ogun; do
    if [ -x "$cand" ]; then
        OGUN_BIN="$cand"
        break
    fi
done

if [ -z "$OGUN_BIN" ]; then
    echo "[$(date -Is)] ogun CLI not found, skipping" >> "$LOG_FILE"
    exit 0
fi

# Optional args from /etc/default/rover-auto-update.
OGUN_UPDATE_ARGS=""
if [ -f /etc/default/rover-auto-update ]; then
    # shellcheck disable=SC1091
    source /etc/default/rover-auto-update
fi

echo "[$(date -Is)] auto-update start" >> "$LOG_FILE"
if "$OGUN_BIN" update ${OGUN_UPDATE_ARGS:-} >> "$LOG_FILE" 2>&1; then
    echo "[$(date -Is)] auto-update success" >> "$LOG_FILE"
else
    echo "[$(date -Is)] auto-update failed" >> "$LOG_FILE"
    exit 1
fi
