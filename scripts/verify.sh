#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "[verify] Shell syntax"
bash -n ogun
while IFS= read -r f; do
  bash -n "$f"
done < <(find scripts pi_server/scripts -maxdepth 1 -type f -name '*.sh' | sort)

echo "[verify] WebUI ignition control wiring"
if grep -q 'id="estop-btn"' pi_server/webui/index.html; then
  echo "[verify] FAIL: stale estop-btn id still present"
  exit 1
fi
grep -q 'id="ignition-btn"' pi_server/webui/index.html
grep -q "type: 'ignition_start'" pi_server/webui/index.html
grep -q "type: 'estop_clear'" pi_server/webui/index.html

echo "[verify] Tune dialog camera role controls"
grep -q 'id="btnTuneSwapCams"' pi_server/webui/index.html
grep -q 'id="tuneCameraRole"' pi_server/webui/index.html

echo "[verify] Protocol/server command support"
grep -q 'IGNITION_START' pi_server/src/Protocol.hpp
grep -q 'ESTOP_CLEAR' pi_server/src/Protocol.hpp
grep -q 'type == RoverCmd::IGNITION_START' pi_server/src/main.cpp
grep -q 'type == RoverCmd::ESTOP_CLEAR' pi_server/src/main.cpp

echo "[verify] OK"
