"use client";
import { useState } from "react";
import { DownloadIcon } from "./icons";
const commands = {
  "Ubuntu / Debian": "sudo apt install build-essential qt6-base-dev qmake6 libqt6dbus6 upower",
  "Fedora": "sudo dnf install gcc-c++ make qt6-qtbase-devel upower",
  "Arch Linux": "sudo pacman -S --needed base-devel qt6-base upower",
};
export default function Install() {
  const [system, setSystem] = useState<keyof typeof commands>("Ubuntu / Debian");
  const [copied, setCopied] = useState("");
  async function copy(text: string) {
    try { await navigator.clipboard.writeText(text); setCopied("Copied"); }
    catch { setCopied("Select and copy the command below."); }
    window.setTimeout(() => setCopied(""), 2500);
  }
  return <section className="install-section section" id="download">
    <div className="install-intro"><span className="eyebrow">05 / YOUR MACHINE, YOUR SOURCE</span><h2>Make room for<br/><em>a little utility.</em></h2><p>Download the current source, inspect what it does, and build it on your Linux machine.</p><a className="button button-light" href="/downloads/evereye-0.4.0-source.tar.gz" download><DownloadIcon /> Download source <span className="button-version">v0.4.0</span></a><p className="install-footnote">MIT licensed · Native Qt 6 · No account needed</p></div>
    <div className="install-instructions"><div className="install-heading"><span>Build it on your machine</span><span className="source-badge">SOURCE BUILD</span></div>
      <div className="os-picker" aria-label="Choose dependency instructions">{Object.keys(commands).map(name => <button key={name} aria-pressed={system === name} onClick={() => {setSystem(name as keyof typeof commands);setCopied("");}}>{name}</button>)}</div>
      <p className="install-step"><span>01</span> Install build dependencies</p><div className="command"><code>{commands[system]}</code><button onClick={() => copy(commands[system])} aria-label="Copy dependency command">Copy</button></div>
      <p className="install-step"><span>02</span> Extract the download, build, and run</p><div className="command"><code>{"tar -xzf evereye-0.4.0-source.tar.gz\ncd evereye-0.4.0\n./scripts/build.sh\n./build/evereye"}</code><button onClick={() => copy("tar -xzf evereye-0.4.0-source.tar.gz\ncd evereye-0.4.0\n./scripts/build.sh\n./build/evereye")} aria-label="Copy build commands">Copy</button></div>
      <p className="copy-status" role="status">{copied || "Requires Qt 6.2+, a user D-Bus session, and logind or elogind."}</p><p className="release-note">Early Linux release. Prebuilt universal packages are still being validated. Desktop lid policies vary; test on your own machine. <a href="/downloads/SHA256SUMS">Verify the source checksum ↗</a></p>
    </div>
  </section>;
}
