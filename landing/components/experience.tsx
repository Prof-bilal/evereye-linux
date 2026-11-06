"use client";
import Link from "next/link";
import { useEffect, useRef, useState } from "react";
import { Arrow, Eye } from "./icons";

export function SiteHeader() {
  const [day, setDay] = useState(false);
  function changeTheme() {
    setDay(value => !value);
    document.documentElement.dataset.theme = day ? "night" : "day";
  }
  return <header className="site-header wrap"><Link href="/" className="wordmark" aria-label="EverEye home"><Eye/>evereye<span className="brand-platform">/ linux</span></Link><div className="header-actions"><button className="theme-toggle" onClick={changeTheme} aria-pressed={day} aria-label="Toggle daylight theme"><span aria-hidden="true">{day ? "◐" : "☼"}</span> {day ? "lights down" : "lights up"}</button><a href="#download" className="nav-download">get the source <Arrow diagonal/></a></div></header>;
}

export function WakeEye() {
  const [awake, setAwake] = useState(false);
  const [offset, setOffset] = useState({ x: 0, y: 0 });
  const region = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    const onScroll = () => setAwake(window.scrollY > 25);
    onScroll(); window.addEventListener("scroll", onScroll, { passive: true });
    return () => window.removeEventListener("scroll", onScroll);
  }, []);
  return <button ref={region} className={`wake-eye ${awake ? "is-awake" : ""}`} aria-label={awake ? "Let the illustration rest" : "Wake the eye illustration"} aria-pressed={awake} onClick={() => setAwake(value => !value)} onPointerMove={event => {
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches || event.pointerType === "touch") return;
    const box = region.current?.getBoundingClientRect(); if (!box) return;
    setOffset({ x: (event.clientX - box.left - box.width / 2) / box.width * 22, y: (event.clientY - box.top - box.height / 2) / box.height * 18 });
  }} onPointerLeave={() => setOffset({ x: 0, y: 0 })}>
    <svg viewBox="0 0 600 230" fill="none" aria-hidden="true"><defs><clipPath id="eye-opening"><path d="M44 115C110 15 221 17 300 17s190-2 256 98c-66 100-177 98-256 98S110 215 44 115Z"/></clipPath></defs><path className="eye-orbit" d="M6 115C98-21 217 0 300 0s202-21 294 115c-92 136-211 115-294 115S98 251 6 115Z"/><g className="eye-open"><path className="eye-outline" d="M44 115C110 15 221 17 300 17s190-2 256 98c-66 100-177 98-256 98S110 215 44 115Z"/><g clipPath="url(#eye-opening)"><g style={{ transform: `translate(${offset.x}px, ${offset.y}px)` }}><circle className="eye-iris" cx="300" cy="115" r="83"/><circle className="eye-iris-ring" cx="300" cy="115" r="65"/><circle className="eye-pupil" cx="300" cy="115" r="36"/><circle className="eye-glint" cx="319" cy="95" r="8"/></g></g></g><path className="eye-closed" d="M44 112c126 58 386 58 512 0"/></svg>
    <span className="eye-coordinate eye-coordinate-left">LOCAL / AWAKE</span><span className="eye-coordinate eye-coordinate-right">{awake ? "STATUS: WATCHING" : "STATUS: RESTING"}</span>
  </button>;
}

export function ChapterNav() {
  const [active, setActive] = useState("main");
  useEffect(() => {
    const observer = new IntersectionObserver(entries => {
      for (const entry of entries) if (entry.isIntersecting) setActive(entry.target.id);
    }, { rootMargin: "-10% 0px -55% 0px", threshold: 0 });
    for (const id of ["main", "how-it-works", "lid", "features", "recovery", "download"]) {
      const element = document.getElementById(id); if (element) observer.observe(element);
    }
    return () => observer.disconnect();
  }, []);
  return <nav className="chapter-nav" aria-label="Page chapters">{[["main","↑","top"],["how-it-works","01","tray"],["lid","02","lid"],["features","03","battery"],["recovery","04","recovery"],["download","05","install"]].map(([id,n,label]) => <a key={id} href={`#${id}`} aria-current={active === id ? "location" : undefined}><span>{n}</span>{label}</a>)}</nav>;
}
