"use client";
import { useEffect, useState } from "react";
import { Eye } from "./icons";

export default function ProductDemo() {
  const [remaining, setRemaining] = useState(6138);
  const [active, setActive] = useState(true);
  const [custom, setCustom] = useState(false);
  const [duration, setDuration] = useState({ days: 0, hours: 0, minutes: 30 });
  const [error, setError] = useState("");
  useEffect(() => {
    if (!active) return;
    const timer = window.setInterval(() => setRemaining(value => Math.max(0, value - 1)), 1000);
    return () => window.clearInterval(timer);
  }, [active]);
  useEffect(() => { if (remaining === 0) setActive(false); }, [remaining]);
  function add(seconds: number) {
    setError("");
    const total = (active ? remaining : 0) + seconds;
    if (seconds < 60 || total > 30 * 86400) { setError("Choose 1 minute to 30 days in total."); return; }
    setRemaining(total); setActive(true);
  }
  const time = [Math.floor(remaining / 3600), Math.floor(remaining / 60) % 60, remaining % 60].map(n => String(n).padStart(2, "0")).join(":");
  return <div className="desktop-scene">

    <div className="desktop-bar"><span className="desktop-name">Workspace 01</span><div><span className="tray-eye"><Eye /></span><span aria-hidden="true">◔ &nbsp; ▰</span><span>Thu, 14:37</span></div></div>
    <div className="app-preview" aria-label="Interactive EverEye preview">
      <div className="app-top"><span><Eye /> EverEye</span><button className="demo-gear" aria-label="Toggle custom duration" onClick={() => setCustom(value => !value)}>⚙</button></div>
      <div className="app-status"><span className={active ? "status-dot" : "status-dot idle"} />{active ? "Your system is awake" : "Ready when you are"}</div>
      <p className="app-description">{active ? "Automatic sleep is disabled." : "Choose a duration to start a session."}</p>
      <div className="demo-timer" data-testid="demo-timer">{active ? time : "00:00:00"}</div><div className="remaining-label">{active ? "Time remaining" : "Session ended"}</div>
      <div className="demo-presets">{[15, 30, 60, 120].map(n => <button key={n} aria-label={`${active ? "Add" : "Start"} ${n} minutes in preview`} onClick={() => add(n * 60)}>{n < 60 ? `${n}m` : `${n / 60}h`}</button>)}</div>
      <button className="custom-toggle" aria-expanded={custom} onClick={() => setCustom(value => !value)}>+ Custom duration <span>{custom ? "−" : "+"}</span></button>
      {custom && <form className="demo-custom" onSubmit={event => { event.preventDefault(); add(duration.days * 86400 + duration.hours * 3600 + duration.minutes * 60); }}>
        <div>{(["days", "hours", "minutes"] as const).map(unit => <label key={unit}>{unit}<input type="number" min="0" max={unit === "days" ? 30 : unit === "hours" ? 23 : 59} value={duration[unit]} onChange={event => setDuration(value => ({ ...value, [unit]: Number(event.target.value) }))}/></label>)}</div><button type="submit">{active ? "Add time" : "Start session"}</button>
      </form>}
      {error && <p className="demo-error" role="alert">{error}</p>}
      <button className={active ? "end-session" : "restart-session"} onClick={() => active ? setActive(false) : add(1800)}>{active ? <><span aria-hidden="true">■</span> End session</> : "Start a 30-minute session"}</button>
      <div className="demo-safety"><span>♧ <span>Battery safeguard<small>Ends below 10%</small></span></span><span className="safety-ok">✓ On</span></div>
    </div>
    <div className="preview-label">INTERACTIVE PREVIEW · NO SYSTEM CHANGES</div>
  </div>;
}
