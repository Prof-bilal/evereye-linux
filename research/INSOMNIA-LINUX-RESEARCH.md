# Insomnia on Linux: source audit and package research

Research date: 2026-11-05. Audience: developer building the Linux edition.
Status at the research stage: source investigation and implementation design. The Linux application has since been implemented; see its [README](../linux/README.md) and [current validation](../linux/docs/VALIDATION.md).

## Result and recommendation

Build a native Linux edition with **Qt6 Widgets, C++20, QtDBus, and CMake**, initially targeting Omarchy/Arch with Hyprland. Preserve the macOS implementation as the behavior reference. Reuse its session rules and recovery invariants by translating and testing them; replace its platform integrations.

Linux already supplies the important underlying services. The work is coordinating them correctly, restoring owned changes, and presenting capability failures. A universal package alone does not reproduce Insomnia's full feature set.

The proposed first release contains timed awake sessions, extend/end controls, tray and window access, battery cutoff, optional power-saver holds, notifications, persisted state, and recovery. App freezing, hotspot switching, browser relaunch, and compositor-specific display changes follow after the core works.

## Source acquisition and verification

The requested command `git clone https://github.com/Prof-bilal/Insomnia.git` failed because the shell could not resolve github.com. The GitHub connector retrieved the recursive file listing and each text blob instead.

- Local source snapshot: [Insomnia](../Insomnia/README.md).
- Remote main resolved to commit [b8e7e20d617f008179583918599669fc1c13a731](https://github.com/Prof-bilal/Insomnia/commit/b8e7e20d617f008179583918599669fc1c13a731), committed 2026-10-27.
- Commit metadata identifies root tree `6b8422ba27d5990b5fdfe262255e3449ed294a86`.
- 169 text files were saved and checked against their individual Git blob SHA-1 values: zero mismatches.
- Two binary assets were omitted: `Resources/AppIcon-1024.png` and `Resources/AppIcon.icns`.
- This is **a source snapshot, not a Git clone**. It contains no commit history, refs, tags, or nested Git repository.
- [Acquisition manifest](../insomnia-source-manifest.json) preserves the returned recursive listing, file hashes, modes, omissions, and resolved commit metadata.
- [Source inventory](SOURCE-INVENTORY.md) lists production modules and test declarations. Inventory coverage does not mean every test assertion was manually reviewed.

When terminal network access is available, clone into a separate directory, check out the commit above, verify the text hashes against the manifest, and retain this report. Do not clone over the snapshot. The outer OpenEye directory also did not resolve as a usable Git repository during verification.

## Observed repository structure

The snapshot contains 64 production Swift files and 16,336 production lines. There are 51 `*Tests.swift` files with 1,284 `func test...` declarations; this is a static count, not an executed test result.

| Area | Evidence | Responsibility and Linux implication |
|---|---|---|
| Build | [Package.swift](../Insomnia/Package.swift) | Swift tools 6.2, macOS 26, executable plus test targets. No external Swift package dependencies are declared. Apple frameworks are the main dependencies. |
| Entry | [main.swift](../Insomnia/Sources/Insomnia/main.swift), [InsomniaApp.swift](../Insomnia/Sources/Insomnia/InsomniaApp.swift) | Recovery CLI is checked before launching AppKit/SwiftUI. Accessory app, tray controls, settings window, startup reconciliation, deferred quit. Replace UI and lifecycle integration. |
| Orchestration | [SessionManager.swift](../Insomnia/Sources/Insomnia/Core/SessionManager.swift), [AppServices.swift](../Insomnia/Sources/Insomnia/Core/AppServices.swift) | Start, extend, end, deadline, recovery, observer wiring, cancellation, error reporting. SessionManager alone is 3,209 lines; translating it as one class would carry substantial complexity. |
| Rules | [SessionMath.swift](../Insomnia/Sources/Insomnia/Core/SessionMath.swift), [FloorRules.swift](../Insomnia/Sources/Insomnia/Core/FloorRules.swift), [Config.swift](../Insomnia/Sources/Insomnia/Model/Config.swift) | Mostly portable behavior, although types include UI and Apple thermal concepts. Translate rules into a platform-independent core. |
| Storage | [Store.swift](../Insomnia/Sources/Insomnia/Store/Store.swift), [RuntimeState.swift](../Insomnia/Sources/Insomnia/Model/RuntimeState.swift), [RecoveryLock.swift](../Insomnia/Sources/Insomnia/Core/RecoveryLock.swift) | Atomic JSON writes, recorded ownership, cross-process serialization. Use Linux paths, versioned schemas, durable writes, and one shared recovery lock. |
| Lid actions | [LidActions.swift](../Insomnia/Sources/Insomnia/Core/LidActions.swift) | Journal before darken/mute/freeze actions, reject stale events, restore in reverse order. Translate transaction semantics. |
| Power | [SleepGuard.swift](../Insomnia/Sources/Insomnia/Core/SleepGuard.swift), [PowerMonitor.swift](../Insomnia/Sources/Insomnia/System/PowerMonitor.swift) | Privileged pmset changes and IOKit/Apple thermal monitoring. Entire adapters need replacement. |
| Recovery/install | [LaunchdBackstop.swift](../Insomnia/Sources/Insomnia/Core/LaunchdBackstop.swift), [backstop.sh](../Insomnia/scripts/backstop.sh), [install.sh](../Insomnia/scripts/install.sh), [uninstall.sh](../Insomnia/scripts/uninstall.sh) | launchd, codesign, sudoers, BSD/macOS tools. Design Linux service and packaging; these scripts are not Linux installers. |
| Desktop integrations | [System](../Insomnia/Sources/Insomnia/System/AudioControl.swift), [UI](../Insomnia/Sources/Insomnia/UI/SettingsView.swift) | AppKit, CoreAudio, CoreWLAN, Security/Keychain, ServiceManagement and private display/keyboard APIs. Replace adapters, retain user-facing concepts. |
| Validation | [Tests](../Insomnia/Tests/InsomniaTests/SessionMathTests.swift), [release validation](../Insomnia/docs/release-validation.md), [CI](../Insomnia/.github/workflows/ci.yml) | Existing tests provide scenarios, not Linux execution coverage. Linux CI currently checks scripts/workflows; the application remains macOS-only. |

## Behavior traced from source

**Start:** UI requests a duration; SessionManager serializes the operation under its recovery lock, writes session and ownership state, confirms the backstop, then disables sleep. A failure after a potentially applied power change goes through recovery rather than simply deleting its journal. A pending end invalidates older in-flight operations.

**Extend:** confirm recovery availability, persist the later deadline, then update the visible session and timer. SessionMath accepts a minimum of 60 seconds; Config defaults to a four-hour session and a 30-day maximum.

**Close/open lid:** only active sessions trigger actions. The source debounces lid events by two seconds and rejects obsolete close operations. Close can darken display/keyboard, mute audio, stop selected app processes, optionally stop idle Docker Desktop, and request Low Power Mode. Open reverses owned lid actions while the session continues. Display and audio restoration have their own persistent failure cases.

**End/quit:** remove the session record before undoing recorded effects. Failed undo remains on disk. Quit may be refused when the app alone must finish cleanup. Disconnected audio devices are treated separately from failures requiring the app to remain alive.

**Battery/heat:** defaults request Low Power Mode below 40% and end below 10%, on battery; a zero end floor disables that cutoff. Two consecutive unreadable battery observations can end a session. No battery is a separate state. Serious Apple thermal state requests Low Power Mode; critical ends the session. These are not generic Linux temperature thresholds.

**Recovery:** launchd runs a backstop every minute. App and backstop share the journal and lock. Process ownership includes PID, start identity, and boot session. Already-stopped processes are not adopted; unverifiable records are not blindly resumed. The backstop cannot restore audio or monitor battery/heat; those limitations appear in the original README.

**Extras:** network failover starts after a five-second outage, then retries with backoff. Hotspot passwords use Keychain. tmux defaults to a 90-second outage threshold, requires individually marked panes, and does not press Enter by default. Browser relaunch preserves selected arguments and requires user action. App Nap overrides are optional and recorded for restoration.

## Linux feature-to-component map

References below were checked through official documentation or primary repositories on 2026-11-05. They establish available APIs, not tested behavior on this laptop.

| Original feature | Linux component | Decision and limitation |
|---|---|---|
| Timed awake session | systemd-logind `Inhibit()`; [S1, S2] | Own the returned Unix FD in the controller; release it at end or controller exit. Distinguish ordinary sleep from low-level lid handling. |
| Keep running with closed lid | `handle-lid-switch` inhibitor plus needed sleep inhibition; [S1–S3] | Desktop power managers may own lid policy. Verify GNOME/KDE/Hyprland separately. No permanent logind.conf edit should be needed for the proposed design. |
| Lid state | login1 `LidClosed`; [S2] | Query initial state, confirm change notification behavior on the installed service, debounce events, and expose unavailable state. |
| Countdown/tray/settings | Qt6 Widgets `QSystemTrayIcon`; [S4] | Supply an accessible settings/control window and CLI if no tray host exists. GNOME has tray/activation limitations. A macOS-style live text item is not uniformly portable. |
| D-Bus inhibitor FD ownership | QtDBus `QDBusUnixFileDescriptor`; [S5] | Verify FD-passing capability; prevent child descriptor inheritance and premature local closure. |
| Battery/AC/watts | UPower; [S6] | Detect actual system batteries and missing/stale readings. UPower device temperature is battery temperature, not CPU thermal state. |
| Low Power Mode | power-profiles-daemon `HoldProfile("power-saver", ...)`; [S7] | Release the owned hold. Honor manual profile changes and release notifications. Probe available profiles and provider compatibility; missing support is optional. |
| Display / keyboard light | brightnessctl; compositor-specific display-power adapter; [S8, S9] | Enumerate devices and check access. Brightness zero is not equivalent to display power off. Avoid turning off external monitors unintentionally. |
| Audio mute/restore | PipeWire/WirePlumber, `wpctl`; [S10] | Suitable CLI prototype; production restoration must resolve stable device identity instead of saving a transient node ID or repeatedly targeting the current default. |
| App freeze | owned systemd user units/cgroup v2; [S11] | Off by default. Never freeze broad process-name matches or the whole user session. Existing arbitrary GUI processes may not have a safely isolated group. |
| Recovery | systemd user controller and recovery job; [S12] | FD-based sleep locks disappear on controller death, but persistent brightness/audio/freeze changes still need independent recovery. |
| Hotspot failover | NetworkManager connection profiles; [S13] | Prefer activating an already configured Wi-Fi profile so NetworkManager manages its secrets. A phone hotspot must actually be broadcasting. |
| Custom secrets | Secret Service; [S14] | Optional if NetworkManager stores the connection. Verify the running provider; installed keyring packages do not prove a live service. Do not assume Keychain's per-app ACL model transfers. |
| Notifications | freedesktop Notifications over session D-Bus; [S15] | Keep persistent errors in the app too; notifications may be suppressed. |
| Thermal rules | kernel thermal/hwmon interfaces, optional lm_sensors; [S16] | No universal equivalent of Apple's four thermal states. Hardware-aware thresholds, validity checks and unknown-state UI are necessary. |
| tmux nudge | tmux | Retain exact pane opt-in, cancellation, dedicated-pane guidance, and Enter-off default. Fix executable discovery for Linux. |
| Chromium flags | Linux browser adapter | Revalidate supported flags against the installed browser; sleep inhibition does not guarantee browser work continues. Defer relaunch in the first release. |
| App Nap | No direct port | Omit this macOS-specific preferences feature; resource policy and browser throttling are different concerns. |
| Docker Desktop freeze | No default Linux equivalent | Linux Docker Engine is a service, not the macOS Desktop process tree. Leave containers and daemon untouched in the initial Linux edition. |

## Existing Linux keep-awake applications

- **systemd-inhibit** is already a useful core primitive and CLI comparison baseline [S3]. It does not provide Insomnia's journal, restoration UI, or optional integrations.
- **GNOME Caffeine** provides suspend/screensaver controls and a timer [S17]. It is relevant for GNOME behavior comparisons, but does not target Hyprland and cannot be assumed to implement Insomnia's complete recovery design. Its repository identifies GPL-2.0 licensing; do not copy its code into an MIT project without addressing that license.
- **caffeine-ng** is another relevant project. Its maintainer identifies Codeberg as the new home [S18]. Direct Codeberg inspection was blocked by robots policy in this session, so no current compatibility, maintenance, or dependency claim is made.

Recommendation: compose Linux services directly. Use existing applications as behavioral references, rather than stacking another tray application underneath Insomnia.

## Local environment evidence

Read-only `/etc/os-release`, `pacman -Q`, executable discovery, and pkg-config checks report:

| Component | Installed version/status |
|---|---|
| Distribution | Omarchy 4.0.6, Arch family |
| Qt6 base / declarative | 6.12.0-3 / 6.12.0-2 |
| Qt6 Core, DBus, Widgets development metadata | pkg-config reports 6.12.0 for all three |
| systemd | 262.1-1 |
| UPower | 1.91.5-1 |
| power-profiles-daemon | 0.30-1 |
| NetworkManager | 1.58.1-2 |
| WirePlumber | 0.5.18-1 |
| brightnessctl | 0.5.1-3 |
| libnotify | 0.8.9-1 |
| lm_sensors | 3.6.2-1 |
| tmux | 3.8-1 |
| GNOME Keyring / KWallet | Installed; live service not checked |
| GCC | Installed |
| CMake / Ninja | Not installed or found on PATH |
| PySide6 | Not installed |
| TLP / tuned | Not installed |

The native Qt6 route fits the installed development libraries. Arch's official repository lists qt6-base, upower, and brightnessctl [S19–S21]. Exact package names and dependencies for Debian/Fedora remain to be verified before packaging there.

Read-only system-bus queries for lid, AC, and power-profile provider version all failed with **Operation not permitted** in the sandbox. Service presence, permissions, sensor availability, inhibitor behavior, tray hosting, and closed-lid operation remain unverified. No system settings or live workloads were changed.

## Design decisions and unresolved questions

**Recommendation, not implemented:** one per-user controller owns the session and inhibitor FDs; the tray UI and CLI call it through a small D-Bus interface. The controller releases locks on expiry; closing the UI does not silently abandon recovery. Do not automatically reacquire sleep inhibition after a controller crash before reconciling battery, deadline, and journal state.

Treat ordinary awake sessions and closed-lid support as separate capabilities. Screen locking remains an explicit requirement: preventing suspend must not silently prevent a user's session from locking. Prefer releasing only this application's inhibition on expiry. Do not force global suspend just because the timer ended.

Use XDG config/state/runtime paths, atomic mode-0600 writes, mode-0700 private directories, schema versioning, fsync where durability matters, and a shared flock. Separate temporary lease records from changes that require undo. Recovery must run before new mutations, retain failed entries, and avoid restoring over later user choices.

The technology choice can change if a GNOME-only extension is desired (GJS becomes attractive), rapid Python development is preferred (PySide6), or a Rust codebase is required (Rust/D-Bus plus a separate tray/UI solution needs its own evaluation). For the observed Omarchy environment, Qt6 offers the most direct fit.

Open questions for implementation, with defaults chosen in the plan: start on Arch/Hyprland; expand only after desktop tests; first release excludes automatic app freezing and network switching; thermal protection is not advertised until sensor mapping is validated.

## Validation limits

This was a source and portability audit, not a security certification or a full manual review of all 1,284 test bodies. Core lifecycle, ownership, configuration, lid, power, storage, and integration boundaries were traced; the full text tree was inventoried.

The macOS Swift build/tests were not run: the required Apple frameworks and Swift toolchain are unavailable here. Linux behavior tests cannot exist until implementation. Network failures prevented a real clone. See [implementation plan](LINUX-IMPLEMENTATION-PLAN.md) for concrete acceptance checks.

## Source ledger

All entries accessed 2026-11-05. Most upstream API pages are living documents; exact update dates are not consistently provided. Installed package versions above are local observations, not inferred from current website versions.

| ID | Source / publisher | Claim used |
|---|---|---|
| S1 | [Inhibitor Locks — systemd project](https://github.com/systemd/systemd/blob/main/docs/INHIBITOR_LOCKS.md) | Lock types and FD lifetime/crash release. |
| S2 | [login1 API — systemd](https://freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html) | Manager API, lid state, authorization/version considerations. |
| S3 | [logind.conf — systemd](https://www.freedesktop.org/software/systemd/man/253/logind.conf.html) and [systemd-inhibit — systemd](https://www.freedesktop.org/software/systemd/man/latest/systemd-inhibit.html) | High-level versus low-level inhibition; version-253 configuration reference is not treated as latest-version proof. |
| S4 | [QSystemTrayIcon — Qt](https://doc.qt.io/qt-6/qsystemtrayicon.html) | Tray availability and desktop-specific limitations; search-indexed official documentation was available, later full-page fetch timed out. |
| S5 | [QDBusUnixFileDescriptor — Qt](https://doc.qt.io/qt-6/qdbusunixfiledescriptor.html) and [QDBusConnection — Qt](https://doc.qt.io/QT-6/qdbusconnection.html) | Unix FD transport and capability check; official indexed documentation. |
| S6 | [UPower Device API — UPower](https://upower.freedesktop.org/docs/Device.html) | Device types, battery percentage, energy rate and battery temperature semantics. |
| S7 | [PowerProfiles API — power-profiles-daemon](https://upower.pages.freedesktop.org/power-profiles-daemon/gdbus-org.freedesktop.UPower.PowerProfiles.html) | Profile holds, releases, caller exit, and manual override. |
| S8 | [brightnessctl manual — Arch](https://man.archlinux.org/man/brightnessctl.1.en) | Brightness CLI reference. |
| S9 | [Dispatchers — Hyprland](https://wiki.hypr.land/configuring/core/dispatchers/) | Compositor-specific display power; installed-version syntax still needs checking. |
| S10 | [wpctl manual — WirePlumber](https://pipewire.pages.freedesktop.org/wireplumber/man/wpctl.html) | Mute/volume commands and object addressing. |
| S11 | [cgroup v2 — Linux kernel](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html) | Process grouping and freezer interface; suitability for selected user units is a design inference. |
| S12 | [systemctl — systemd](https://www.freedesktop.org/software/systemd/man/latest/systemctl.html) | User manager/service operations; proposed controller/recovery architecture is our design. |
| S13 | [NetworkManager D-Bus API — NetworkManager](https://networkmanager.dev/docs/api/latest/spec.html) | Connections, devices, and secret-agent interfaces. |
| S14 | [Secret Service API — freedesktop.org](https://specifications.freedesktop.org/secret-service/latest/) | Collections, items, prompts and locking; current landing page identifies 0.2 draft dated 2026-10-19. Implement against deployed capabilities. |
| S15 | [Notifications specification — freedesktop.org](https://specifications.freedesktop.org/notification/latest-single/) | Session-bus notification protocol. |
| S16 | [Thermal sysfs API — Linux kernel](https://docs.kernel.org/driver-api/thermal/sysfs-api.html) | Thermal zones and trip points. |
| S17 | [GNOME Caffeine — project repository](https://github.com/eonpatapon/gnome-shell-extension-caffeine) | GNOME keep-awake/timer comparison and license. |
| S18 | [caffeine-ng migration announcement — project maintainer](https://whynothugo.nl/journal/2022/07/12/announcing-caffeine-ng-v4.0.0/) | 2022 migration to Codeberg only; not current maintenance evidence. |
| S19 | [qt6-base — Arch package repository](https://archlinux.org/packages/extra/x86_64/qt6-base/) | Official package availability. |
| S20 | [upower — Arch package repository](https://archlinux.org/packages/extra/x86_64/upower/) | Official package availability. |
| S21 | [brightnessctl — Arch package repository](https://archlinux.org/packages/extra/x86_64/brightnessctl/) | Official package availability. |
