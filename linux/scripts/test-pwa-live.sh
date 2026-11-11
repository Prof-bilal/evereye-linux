#!/usr/bin/env bash
# End-to-end: real daemon + real gateway + real browser + the built PWA.
# Run under a private bus: dbus-run-session -- ./scripts/test-pwa-live.sh
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mobile="$here/../mobile"
[[ -f "$mobile/out/index.html" ]] || { echo "Build the PWA first: (cd ../mobile && npm run build)" >&2; exit 1; }
[[ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]] || { echo "Run under: dbus-run-session -- $0" >&2; exit 2; }

binary=${EVEREYE_BINARY:-$here/build/evereye}
[[ -x "$binary" ]] || { echo "Build first: ./scripts/build.sh" >&2; exit 1; }

tmp=$(mktemp -d)
pids=()
cleanup() { for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null || true; done; rm -rf "$tmp"; }
trap cleanup EXIT

token=$(printf 'b%.0s' {1..64})
port=$(python3 - <<'PY'
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PY
)

mkdir -p "$tmp/config/evereye" "$tmp/data/evereye/web"
cp -R "$mobile/out/." "$tmp/data/evereye/web/"
cat > "$tmp/config/evereye/tasks.json" <<'JSON'
[ { "name": "live-preset", "command": "sh", "args": ["-c", "printf live-pwa-marker; sleep 3"] } ]
JSON
cat > "$tmp/config/evereye/gateway.json" <<JSON
{ "token": "$token", "bind": "127.0.0.1", "port": $port, "enabled": true, "selfSigned": false, "allowArbitraryCommands": false }
JSON

export XDG_CONFIG_HOME="$tmp/config" XDG_STATE_HOME="$tmp/state" XDG_DATA_HOME="$tmp/data"
"$binary" --daemon >/dev/null 2>&1 & pids+=($!)
"$binary" gateway --bind 127.0.0.1 --port "$port" >/dev/null 2>&1 & pids+=($!)

ready=0
for _ in $(seq 1 50); do
  if curl -fsS -o /dev/null -H "Authorization: Bearer $token" "http://127.0.0.1:$port/api/status" 2>/dev/null; then ready=1; break; fi
  sleep 0.2
done
[[ "$ready" == 1 ]] || { echo "gateway did not become ready" >&2; exit 1; }

env -u WAYLAND_DISPLAY -u DISPLAY BASE_URL="http://127.0.0.1:$port" TOKEN="$token" node "$mobile/tests/live-pwa.mjs"
