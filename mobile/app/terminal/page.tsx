"use client";

import { FitAddon } from "@xterm/addon-fit";
import { Terminal } from "@xterm/xterm";
import "@xterm/xterm/css/xterm.css";
import { useEffect, useRef, useState } from "react";

type Config = { url: string; token: string };
const STORAGE = "evereye.mobile.config";

const THEME = {
  background: "#0e1416",
  foreground: "#dcebe5",
  cursor: "#83c7b1",
  selectionBackground: "#2b4a40",
  black: "#182022", red: "#ff6b5e", green: "#83c7b1", yellow: "#e0a53a",
  blue: "#5aa9ff", magenta: "#d98cff", cyan: "#4cc9b0", white: "#dcebe5",
  brightBlack: "#6f7f79", brightRed: "#ff8a5c", brightGreen: "#a0d8c5",
  brightYellow: "#f0c05a", brightBlue: "#7fbcff", brightMagenta: "#e6aaff",
  brightCyan: "#6fdcc7", brightWhite: "#f4fff9",
} as const;

export default function TerminalPage() {
  const [config, setConfig] = useState<Config | null>(null);
  const [job, setJob] = useState("");
  const [state, setState] = useState<"connecting" | "connected" | "closed" | "error">("connecting");
  const [draftUrl, setDraftUrl] = useState("");
  const [draftToken, setDraftToken] = useState("");
  const [loaded, setLoaded] = useState(false);
  const host = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    setJob(new URLSearchParams(window.location.search).get("id") || "");
    try {
      const raw = localStorage.getItem(STORAGE);
      if (raw) {
        const parsed = JSON.parse(raw) as Config;
        setConfig(parsed);
        setDraftUrl(parsed.url);
        setDraftToken(parsed.token);
      } else if (/^https?:$/.test(window.location.protocol)) {
        setDraftUrl(window.location.origin);
      }
    } catch {
      /* show the setup form */
    }
    setLoaded(true);
  }, []);

  useEffect(() => {
    if (!config || !job || !host.current) return;
    const terminal = new Terminal({
      fontFamily: "'JetBrains Mono', 'DejaVu Sans Mono', ui-monospace, monospace",
      fontSize: 13,
      cursorBlink: true,
      scrollback: 5000,
      theme: THEME,
    });
    const fit = new FitAddon();
    terminal.loadAddon(fit);
    terminal.open(host.current);
    fit.fit();

    const base = config.url.replace(/\/$/, "");
    const wsUrl = base.replace(/^http/, "ws") + "/api/jobs/" + encodeURIComponent(job) + "/terminal";
    setState("connecting");
    const socket = new WebSocket(wsUrl, ["evereye", config.token]);
    socket.binaryType = "arraybuffer";
    const sendSize = () => {
      if (socket.readyState === WebSocket.OPEN)
        socket.send(JSON.stringify({ type: "resize", cols: terminal.cols, rows: terminal.rows }));
    };
    socket.onopen = () => {
      setState("connected");
      fit.fit();
      sendSize();
      terminal.focus();
    };
    socket.onmessage = (event) => {
      if (typeof event.data === "string") return;
      terminal.write(new Uint8Array(event.data as ArrayBuffer));
    };
    socket.onclose = () => setState("closed");
    socket.onerror = () => setState("error");
    const typed = terminal.onData((value) => {
      if (socket.readyState === WebSocket.OPEN) socket.send(new TextEncoder().encode(value));
    });
    const resized = terminal.onResize(sendSize);
    const observer = new ResizeObserver(() => fit.fit());
    observer.observe(host.current);
    return () => {
      observer.disconnect();
      typed.dispose();
      resized.dispose();
      socket.close();
      terminal.dispose();
    };
  }, [config, job]);

  const connect = () => {
    const next = { url: draftUrl.trim().replace(/\/$/, ""), token: draftToken.trim() };
    if (!next.url || !next.token) return;
    localStorage.setItem(STORAGE, JSON.stringify(next));
    setConfig(next);
  };

  return (
    <main className="terminal-shell">
      <header className="terminal-top">
        <a href="/" className="ghost small">← Dashboard</a>
        <span className="muted small mono">{job ? job : "terminal"}</span>
        <span className={`pill ${state === "connected" ? "live" : state === "error" ? "bad" : "idle"}`}>{state}</span>
      </header>
      {!loaded ? null : !config ? (
        <section className="card setup">
          <h1>Connect the terminal</h1>
          <p className="muted">Use the gateway URL and token from <code>~/.config/evereye/gateway.json</code>.</p>
          <label>Gateway URL<input value={draftUrl} onChange={(event) => setDraftUrl(event.target.value)} aria-label="Gateway URL" /></label>
          <label>Token<input value={draftToken} onChange={(event) => setDraftToken(event.target.value)} type="password" aria-label="Token" /></label>
          <button className="primary" onClick={connect}>Connect</button>
        </section>
      ) : !job ? (
        <section className="card">
          <h1>No session selected</h1>
          <p className="muted">Open the dashboard and choose a session.</p>
          <a className="link-button" href="/">Open dashboard</a>
        </section>
      ) : (
        <div className="terminal-surface" ref={host} data-testid="terminal" />
      )}
    </main>
  );
}
