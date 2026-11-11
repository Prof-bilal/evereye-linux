"use client";

import { useCallback, useEffect, useRef, useState } from "react";

type Config = { url: string; token: string };
type Job = { id: string; name: string; status: string; exitCode: number; startedAt: number; endedAt: number; bytesTotal: number };
type Task = { name: string; command?: string };
type Status = { ok?: boolean; active?: boolean; remainingSeconds?: number; batteryPercent?: number; reason?: string; error?: string; runningJobs?: number; [key: string]: unknown };

const STORAGE = "evereye.mobile.config";

function formatDuration(total: number) {
  const seconds = Math.max(0, Math.floor(total));
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return [h, m, s].map((n) => String(n).padStart(2, "0")).join(":");
}

const statusTone = (status: string) =>
  status === "Running" || status === "Starting" ? "live" : status === "Failed" || status === "Killed" ? "bad" : "idle";

// Terminal data carries colour and cursor control sequences; keep the text.
const cleanTerminal = (text: string) =>
  text
    .replace(/\x1b\][^\x07]*(?:\x07|\x1b\\)/g, "")
    .replace(/\x1b\[[0-9;?]*[ -/]*[@-~]/g, "")
    .replace(/\x1b[@-_]/g, "")
    .replace(/\r/g, "");

export default function Page() {
  const [config, setConfig] = useState<Config | null>(null);
  const [draftUrl, setDraftUrl] = useState("");
  const [draftToken, setDraftToken] = useState("");
  const [editing, setEditing] = useState(false);
  const [status, setStatus] = useState<Status>({});
  const [jobs, setJobs] = useState<Job[]>([]);
  const [tasks, setTasks] = useState<Task[]>([]);
  const [arbitrary, setArbitrary] = useState(false);
  const [selected, setSelected] = useState<string>("");
  const [log, setLog] = useState("");
  const [entry, setEntry] = useState("");
  const [preset, setPreset] = useState("");
  const [command, setCommand] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [connected, setConnected] = useState<boolean | null>(null);
  const previousRun = useRef<Record<string, boolean>>({});
  const streamAbort = useRef<AbortController | null>(null);
  const logRef = useRef<HTMLPreElement | null>(null);

  useEffect(() => {
    try {
      const raw = localStorage.getItem(STORAGE);
      if (raw) {
        const parsed = JSON.parse(raw) as Config;
        setConfig(parsed);
        setDraftUrl(parsed.url);
        setDraftToken(parsed.token);
      } else {
        setEditing(true);
        if (typeof window !== "undefined" && /^https?:$/.test(window.location.protocol)) setDraftUrl(window.location.origin);
      }
    } catch {
      setEditing(true);
    }
  }, []);

  const api = useCallback(
    async (path: string, options: RequestInit = {}) => {
      if (!config) throw new Error("Configure the gateway first");
      const response = await fetch(config.url.replace(/\/$/, "") + path, {
        ...options,
        headers: {
          Authorization: "Bearer " + config.token,
          "Content-Type": "application/json",
          ...(options.headers || {}),
        },
      });
      const text = await response.text();
      let data: Record<string, unknown> = {};
      try {
        data = JSON.parse(text);
      } catch {
        data = { ok: false, error: text || "Invalid reply" };
      }
      if (response.status === 401) throw new Error("Token rejected");
      return data as Record<string, unknown>;
    },
    [config]
  );

  const refresh = useCallback(async () => {
    if (!config) return;
    try {
      const [nextStatus, nextJobs] = await Promise.all([
        api("/api/status"),
        api("/api/jobs"),
      ]);
      setConnected(true);
      setStatus(nextStatus as Status);
      const list = (nextJobs.jobs || []) as Job[];
      setJobs(list);
      for (const job of list) {
        const running = job.status === "Running" || job.status === "Starting";
        if (previousRun.current[job.id] && !running && typeof Notification !== "undefined" && Notification.permission === "granted") {
          new Notification("AI session finished: " + job.name, { body: job.status + (job.exitCode ? " · exit " + job.exitCode : ""), icon: "/icon.svg" });
        }
        previousRun.current[job.id] = running;
      }
      setError("");
    } catch (cause) {
      setConnected(false);
      setError(String(cause instanceof Error ? cause.message : cause));
    }
  }, [api, config]);

  const loadTasks = useCallback(async () => {
    if (!config) return;
    try {
      const data = await api("/api/tasks");
      const list = (data.tasks || []) as Task[];
      setTasks(list);
      setArbitrary(Boolean(data.allowArbitraryCommands));
      if (list.length) setPreset((current) => current || list[0].name);
    } catch {
      /* tasks are optional */
    }
  }, [api, config]);

  useEffect(() => {
    if (!config) return;
    void refresh();
    void loadTasks();
    const timer = setInterval(refresh, 2000);
    return () => clearInterval(timer);
  }, [config, refresh, loadTasks]);

  useEffect(() => {
    if (!config || !selected) return;
    streamAbort.current?.abort();
    const controller = new AbortController();
    streamAbort.current = controller;
    setLog("");
    let buffer = "";
    (async () => {
      try {
        const response = await fetch(`${config.url.replace(/\/$/, "")}/api/jobs/${selected}/stream`, {
          headers: { Authorization: "Bearer " + config.token },
          signal: controller.signal,
        });
        if (!response.ok || !response.body) throw new Error("Stream unavailable");
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          buffer += decoder.decode(value, { stream: true });
          let index;
          while ((index = buffer.indexOf("\n\n")) >= 0) {
            const frame = buffer.slice(0, index);
            buffer = buffer.slice(index + 2);
            const data = frame
              .split("\n")
              .filter((line) => line.startsWith("data:"))
              .map((line) => line.slice(5).trim())
              .join("");
            if (!data || data === "{}") continue;
            try {
              const payload = JSON.parse(data) as { data?: string };
              if (payload.data) setLog((current) => (current + cleanTerminal(payload.data!)).slice(-200000));
            } catch {
              /* keep the frame body on parse failure */
            }
          }
        }
      } catch (cause) {
        if (!controller.signal.aborted) setError(String(cause instanceof Error ? cause.message : cause));
      }
    })();
    return () => controller.abort();
  }, [config, selected]);

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [log]);

  const saveConfig = () => {
    const next = { url: draftUrl.trim().replace(/\/$/, ""), token: draftToken.trim() };
    if (!next.url || !next.token) {
      setError("A gateway URL and token are required.");
      return;
    }
    localStorage.setItem(STORAGE, JSON.stringify(next));
    setConfig(next);
    setEditing(false);
    setError("");
  };

  const act = async (path: string, body?: unknown, method = "POST") => {
    setBusy(true);
    try {
      const data = await api(path, { method, body: body === undefined ? undefined : JSON.stringify(body) });
      if (data.ok === false) setError(String(data.error || "Request failed"));
      else setError("");
      await refresh();
      return data;
    } catch (cause) {
      setError(String(cause instanceof Error ? cause.message : cause));
      return null;
    } finally {
      setBusy(false);
    }
  };

  const sendInput = async () => {
    if (!selected || !entry) return;
    const text = entry;
    setEntry("");
    await act(`/api/jobs/${selected}/input`, { data: text + "\n" });
  };

  const startTerminal = async () => {
    const started = await act("/api/jobs", { shell: true });
    if (started?.ok) {
      const id = String(started.id || "");
      await refresh();
      if (id) setSelected(id);
    }
  };

  const startJob = async () => {
    const body = preset ? { preset } : { command: command.trim() };
    if (!preset && !command.trim()) {
      setError("Choose a preset or enter a command.");
      return;
    }
    const started = await act("/api/jobs", body);
    if (started?.ok) {
      const id = String(started.id || "");
      await loadTasks();
      await refresh();
      if (id) setSelected(id);
    }
  };

  const current = jobs.find((job) => job.id === selected);
  const remaining = Number(status.remainingSeconds || 0);

  if (editing || !config) {
    return (
      <main className="shell">
        <header className="top">
          <div className="brand"><span className="eye" aria-hidden /> EverEye</div>
        </header>
        <section className="card setup">
          <h1>Connect your laptop</h1>
          <p className="muted">
            Start <code>evereye gateway</code> on the machine, then paste its address and token from
            <code> ~/.config/evereye/gateway.json</code>.
          </p>
          <label>
            Gateway URL
            <input
              value={draftUrl}
              onChange={(event) => setDraftUrl(event.target.value)}
              placeholder="http://192.168.1.20:8737"
              inputMode="url"
              autoCapitalize="off"
              autoCorrect="off"
              aria-label="Gateway URL"
            />
          </label>
          <label>
            Token
            <input
              value={draftToken}
              onChange={(event) => setDraftToken(event.target.value)}
              placeholder="64 hex characters"
              type="password"
              autoCapitalize="off"
              autoCorrect="off"
              aria-label="Token"
            />
          </label>
          {error ? <p className="error" role="alert">{error}</p> : null}
          <button className="primary" onClick={saveConfig}>Connect</button>
          {config ? <button className="ghost" onClick={() => setEditing(false)}>Cancel</button> : null}
        </section>
      </main>
    );
  }

  return (
    <main className="shell">
      <header className="top">
        <div className="brand"><span className={`eye ${connected === false ? "off" : ""}`} aria-hidden /> EverEye</div>
        <div className="top-right">
          <span className={`dot ${connected === false ? "bad" : connected ? "ok" : "idle"}`} aria-label={connected ? "Connected" : "Disconnected"} />
          <button className="ghost small" onClick={() => { setEditing(true); setDraftUrl(config.url); setDraftToken(config.token); }}>Settings</button>
        </div>
      </header>

      <section className="card session">
        <div className="row space">
          <span className="muted">AWAKE SESSION</span>
          <span className={`pill ${status.active ? "live" : "idle"}`}>{status.active ? "Active" : "Idle"}</span>
        </div>
        <div className="countdown" data-testid="countdown">{formatDuration(status.active ? remaining : 0)}</div>
        <div className="muted small">
          {status.active ? "Your laptop stays awake." : "No awake session is running."}
          {status.runningJobs ? ` · ${status.runningJobs} AI job${Number(status.runningJobs) === 1 ? "" : "s"} running` : ""}
        </div>
        <div className="actions">
          <button onClick={() => act("/api/session/extend", { seconds: 900 })} disabled={!status.active || busy}>+15 minutes</button>
          <button onClick={() => act("/api/session/extend", { seconds: 3600 })} disabled={!status.active || busy}>+1 hour</button>
          <button className="danger" onClick={() => act("/api/session/end")} disabled={!status.active || busy}>End session</button>
        </div>
      </section>

      <section className="card">
        <div className="row space"><span className="muted">NEW AI SESSION</span></div>
        {tasks.length ? (
          <select value={preset} onChange={(event) => setPreset(event.target.value)} aria-label="Task preset">
            {tasks.map((task) => <option key={task.name} value={task.name}>{task.name}</option>)}
            {arbitrary ? <option value="">Custom command…</option> : null}
          </select>
        ) : null}
        {(!tasks.length || !preset) && arbitrary ? (
          <input value={command} onChange={(event) => setCommand(event.target.value)} placeholder="claude" aria-label="Command" autoCapitalize="off" autoCorrect="off" />
        ) : null}
        <div className="actions">
          <button className="primary" onClick={startJob} disabled={busy}>Start session</button>
          <button onClick={startTerminal} disabled={busy}>New terminal</button>
        </div>
        {!tasks.length ? <p className="muted small">Add presets in <code>~/.config/evereye/tasks.json</code>.</p> : null}
      </section>

      <section className="card">
        <div className="row space"><span className="muted">JOBS</span><span className="muted small">{jobs.length}</span></div>
        <ul className="jobs" role="list">
          {jobs.map((job) => (
            <li key={job.id}>
              <button className={`job ${job.id === selected ? "selected" : ""}`} onClick={() => setSelected(job.id)}>
                <span className="job-name">{job.name}</span>
                <span className={`pill ${statusTone(job.status)}`}>{job.status}</span>
                <span className="muted small mono">{job.id}</span>
              </button>
            </li>
          ))}
          {!jobs.length ? <li className="muted small">No sessions yet.</li> : null}
        </ul>
      </section>

      {current ? (
        <section className="card">
          <div className="row space">
            <span className="muted">OUTPUT · {current.name}</span>
            <button className="danger small" onClick={() => act(`/api/jobs/${current.id}/stop`)} disabled={busy || !(current.status === "Running" || current.status === "Starting")}>Stop</button>
          </div>
          <pre className="log" ref={logRef} data-testid="log">{log || "Waiting for output…"}</pre>
          <form
            className="terminal-input"
            onSubmit={(event) => {
              event.preventDefault();
              void sendInput();
            }}
          >
            <input
              value={entry}
              onChange={(event) => setEntry(event.target.value)}
              placeholder="Type a command and press Enter"
              aria-label="Terminal input"
              autoCapitalize="off"
              autoCorrect="off"
              autoComplete="off"
            />
            <button type="submit" disabled={!entry}>Send</button>
          </form>
        </section>
      ) : null}

      {error ? <p className="error" role="alert">{error}</p> : null}
      <footer className="muted small">
        EverEye keeps your machine awake; this app only talks to its gateway over your network.
      </footer>
    </main>
  );
}
