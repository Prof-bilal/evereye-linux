# AI sessions and the remote gateway

EverEye can own AI coding-agent runs as **jobs**: launch an agent (Claude Code, aider,
codex, or any CLI) under EverEye, keep working with the lid closed, and watch, stop, or
start sessions from a phone.

## Pieces

| Piece | Role |
|---|---|
| `evereye run` | Starts a job in a PTY owned by the controller; output is captured live. |
| `evereye jobs send` | Writes keystrokes/commands to a running job's PTY. |
| `evereye attach` | Bridges this terminal's stdin/stdout to a running job's PTY. |
| New terminal | Starts an interactive `$SHELL` job and opens the web terminal. |
| Open terminal | Opens the xterm.js web terminal attached to the selected session. |
| Controller (`evereye --daemon`) | Owns job state, logs, and the existing awake session. |
| `evereye gateway` | Opt-in HTTP + SSE bridge to the controller over the user's session D-Bus. |
| `mobile/` PWA | Installable phone client served by the gateway or hosted separately. |

A job's output is written to `${XDG_STATE_HOME:-~/.local/state}/insomnia-linux/jobs/<id>.log`
(mode 0600) and kept in a 256 KB in-memory ring so a late attacher still sees recent output.
The journal `${XDG_STATE_HOME}/insomnia-linux/jobs.json` records every job; a job left
"Running" across a restart is marked `Interrupted`, never silently reattached.

## Quick start

```bash
cd linux
./scripts/build.sh

# Start an agent under EverEye (preset defined in tasks.json, or a direct command)
./evereye run -- claude
./evereye jobs
./evereye jobs log <id> -f
./evereye jobs send <id> "ls -la"
./evereye attach <id>          # real terminal window attached to the job
./evereye jobs stop <id>

# Open terminal / New terminal in the app opens the gateway's xterm.js page,
# which streams the PTY over a WebSocket. The embedded pane stays available
# for a quick look, and `evereye attach` remains for a local terminal.

# Start an awake session so the laptop survives a closed lid
./evereye start 4h --lid
```

### Presets

`~/.config/evereye/tasks.json` is an allow-list of commands the phone may start:

```json
[
  { "name": "claude", "command": "claude" },
  { "name": "tests", "command": "sh", "args": ["-c", "pytest -q"], "cwd": "/home/me/project" }
]
```

`~/.config/evereye/jobs.json` optionally tunes limits:

```json
{ "maxJobs": 8, "logMaxBytes": 8388608, "allowArbitraryCommands": false }
```

### Gateway

```bash
./evereye gateway                 # 127.0.0.1:8737, plain HTTP on loopback
./evereye gateway --bind 0.0.0.0  # refuses without TLS
```

The first run creates `~/.config/evereye/gateway.json` with a random bearer token. Edit it
for the bind address, port, TLS, and notification webhooks:

```json
{
  "enabled": true,
  "bind": "0.0.0.0",
  "port": 8737,
  "token": "<64 hex characters>",
  "selfSigned": true,
  "allowArbitraryCommands": false,
  "notify": [ { "url": "https://ntfy.sh/your-secret-topic", "on": ["jobEnd", "jobFail"] } ]
}
```

With `selfSigned` on, EverEye generates `gateway.crt`/`gateway.key` with `openssl` on first
run. Provide `"tls": { "cert": "...", "key": "..." }` for your own certificate. A
non-loopback bind without TLS is refused unless `EVEREYE_INSECURE_ALLOW_PLAINTEXT=1` is set.

The phone reaches the gateway on the same WiFi, or from anywhere through a WireGuard,
Tailscale, or ZeroTier network. `notify` URLs are POSTed a JSON body
`{"event": "jobEnd"|"jobFail", "job": {...}}` when a job finishes, so a locked phone can be
alerted without Web Push.

### Phone client

```bash
cd mobile
npm ci && npm run build          # static export in mobile/out
npm run test:e2e                 # Playwright against a mock gateway
```

If the gateway finds `.../share/evereye/web/index.html` (user or system install) it serves the
PWA at `/`, so the common case is same-origin. Install it from the browser's "Add to Home
Screen". Open Settings and paste the gateway URL and token.

## Real terminal (xterm.js)

Each running job serves a local Unix socket at
`$XDG_RUNTIME_DIR/evereye/jobs/<id>.sock` (mode 0600 in a 0700 directory).
The gateway bridges that socket to a browser WebSocket at
`GET /api/jobs/<id>/terminal`; the PWA page `/terminal/?id=<id>` renders it
with xterm.js (FitAddon), sends `term.onData` back as binary frames, and
forwards `fit()` sizes as `{ "type": "resize", "cols", "rows" }` text frames.

- **Open terminal** / **New terminal** in the desktop app open that page in the
  browser (it defaults to `http://127.0.0.1:<gateway port>/terminal/?id=<id>`).
- The WebSocket token rides as a subprotocol: `new WebSocket(url, ["evereye", token])`;
  browsers cannot set `Authorization` on a WebSocket. A wrong token gets 401.
- Output is binary WebSocket frames fed to `term.write`, so full-screen agents
  render correctly and resizes go through `TIOCSWINSZ` + `SIGWINCH`.
- Closing the page just detaches; the job keeps running and stays visible in
  EverEye and the gateway.
- `evereye attach <id>` is still available for a local terminal emulator; the
  socket checks `SO_PEERCRED`, so only the same Unix user can attach.
- Several viewers can attach to one job. The last resize wins, like tmux.

## HTTP API

All `/api/*` routes require `Authorization: Bearer <token>`.

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Awake session and job counts |
| GET | `/api/jobs` | Every job with status and exit code |
| GET | `/api/jobs/<id>` | One job |
| GET | `/api/jobs/<id>/stream` | Live output as Server-Sent Events |
| WS | `/api/jobs/<id>/terminal` | Bidirectional PTY for the xterm.js page |
| POST | `/api/jobs` | Start `{ "preset": "claude" }` or (if allowed) `{ "command": ..., "args": [...] }` |
| POST | `/api/jobs/<id>/stop` | Terminate the job's process group |
| POST | `/api/jobs/<id>/input` | `{ "data": "text\\n" }` written to the job's PTY |
| GET | `/api/tasks` | Configured presets and whether arbitrary commands are allowed |
| POST | `/api/session/extend` | `{ "seconds": 900 }` |
| POST | `/api/session/end` | End the awake session |

Requests are capped at 8 KB of headers and 64 KB of body; only `GET`, `POST`, and `OPTIONS`
are accepted. The token is compared in constant time before any controller call.

## Security model

The gateway adds **remote command execution by design**. Treat it like an SSH endpoint:

- It is off unless you run `evereye gateway`.
- The token is required for every API call; rotate it by editing `gateway.json`.
- Off-loopback binds require TLS, or run it only over a VPN.
- `allowArbitraryCommands` is false by default, so the phone can start only `tasks.json`
  presets. Set it to true only if you accept full remote control.
- Jobs run as your user with your environment; they are not sandboxed.
- The controller still checks the caller's Unix UID for D-Bus requests, and the gateway only
  relays same-user calls.
- Flatpak: the sandbox grants no network access, so the gateway is a non-Flatpak feature in
  this release. Adding `--share=network` would widen the sandbox and is a separate decision.

## Limits

- Jobs are children of the controller. Ending/shutting down the controller (`evereye
  shutdown`, a crash, or a rebuild) sends `SIGHUP`/`SIGTERM` to the groups and marks jobs
  `Interrupted`. A job supervised by a separate re-exec process is a documented upgrade.
- The UI's free-form command box splits on whitespace; use presets for commands with
  quoted arguments.
- Notifications are outbound webhooks, not Web Push; the PWA shows local notifications
  while it is open.
