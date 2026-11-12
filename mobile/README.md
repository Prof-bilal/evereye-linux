# EverEye mobile

Installable PWA that watches and controls EverEye AI sessions through the
`evereye gateway` on your laptop. Same Next.js + TypeScript toolchain as `../landing`.

## Develop

```bash
npm ci
npm run dev        # http://localhost:3000
npm run typecheck
npm run build      # static export to ./out
npm run test:e2e   # Playwright against tests/mock-server.mjs
```

## Point it at a laptop

Start the gateway on the machine:

```bash
cd ../linux && ./build/evereye gateway
```

The gateway prints its address and the token lives in `~/.config/evereye/gateway.json`.
Open the app, paste the URL (for example `http://192.168.1.20:8737`) and the token. Off your
home network, put both devices on a WireGuard/Tailscale/ZeroTier network first.

If the gateway serves this app itself (from `~/.local/share/evereye/web`), the URL defaults
to the current origin and you only need the token.

## What it does

- Live awake-session countdown with extend and end.
- Jobs list with status, start from presets, stop a job.
- Live terminal output over Server-Sent Events (the token is sent as a header, never in a URL).
- A full xterm.js terminal at `/terminal/?id=<job>` over a WebSocket PTY; the
  token is sent as a WebSocket subprotocol, not in the URL.
- A local notification when a job finishes while the app is open.

Notifications for a locked phone come from the gateway's `notify` webhook (for example
ntfy), not from this PWA.
