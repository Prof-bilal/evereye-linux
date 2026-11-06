# Linux implementation plan

Status: original implementation proposal, retained as the planning record. Development has since started; see the [implemented Linux edition](../linux/README.md) and its [validation record](../linux/docs/VALIDATION.md). Based on [source audit and research](INSOMNIA-LINUX-RESEARCH.md). The source snapshot remains the macOS reference.

## Outcome and scope

Deliver an installable native Linux tray/window application that keeps a session awake until a bounded deadline, supports extending and ending it, handles battery cutoff, and releases its owned inhibition after expiry or controller failure.

Initial target: Omarchy/Arch, Hyprland, systemd-logind, Qt6. Broader GNOME/KDE support requires separate acceptance evidence.

First-release non-goals: arbitrary app freezing, Docker daemon changes, automatic Wi-Fi switching, browser relaunch, App Nap emulation, universal thermal guarantees, and permanent system power-policy edits. They remain candidate later features.

## Ordered steps

Each step is owned by the implementing developer. Proposed file paths below do not exist yet.

| Step | Input and dependency | Proposed output/files | Acceptance evidence |
|---|---|---|---|
| 1. Restore full source checkout | GitHub network access and acquisition manifest | Separate real clone at the recorded commit; preserve audit snapshot | Correct remote and commit, clean checkout, 169 matching text blobs, binary assets and Git metadata present. If network is unavailable, retain snapshot and record acquisition as incomplete. |
| 2. Scaffold Linux target | Audit, Qt6 headers, compiler; obtain CMake/Ninja through normal development setup | `linux/CMakeLists.txt`, `linux/src/core/`, `linux/tests/` | Configure/build with Qt6 Core, DBus, Widgets, Test; no calls to macOS install scripts. Preserve original MIT notice for derived material; review Qt distribution obligations. |
| 3. Port pure rules | SessionMath, FloorRules, Config, selected upstream cases | `core/session.*`, `core/policy.*`, `core/config.*` | Duration bounds, invalid input, extend at/after expiry, disabled cutoff, missing vs unreadable battery, and conflicting rule tests. Linux thermal inputs initially report unsupported. |
| 4. Establish capability model | Running session/system buses | `linux/src/platform/capabilities.*`, doctor CLI | Report inhibitor access, logind ownership, lid read, UPower, power profile support, tray host and optional tools separately. Missing optional service does not prevent basic mode. Inaccessible mandatory service prevents a false active state. |
| 5. Implement inhibition | Step 4; login1 and QtDBus | `platform/logind.*` | Receive real Unix FD; verify ordinary sleep and closed-lid modes separately; close every copy on end and crash; no inheritance to launched child processes. Use a fake service on a private bus in tests. |
| 6. Implement controller and storage | Steps 3–5 | `linux/src/controller/`, `core/journal.*`, versioned D-Bus interface and CLI | One controller per user; serialized start/extend/end; atomic and durable state; shared recovery lock; no expired/previous-boot automatic restart; persist failure rather than discard it. Controller owns monotonic deadline plus durable wall-clock/boot metadata. |
| 7. Battery and optional power saver | Step 6; UPower and supported PowerProfiles provider | `platform/upower.*`, `platform/powerprofile.*` | Multiple power devices, desktop without battery, unplugged AC, stale/unreadable data and service restart cases. Releasing a hold respects a later manual profile choice. Controller crash removes its lease. |
| 8. UI and notifications | Stable controller API | `linux/src/ui/`, desktop file, vector icon | Start, countdown, extend, end, settings, visible errors, keyboard navigation and screen-reader labels. UI reconnects after controller restart. No tray host still leaves window/CLI controls available. Closing UI and explicitly quitting have documented distinct behavior. |
| 9. Packaging and controller lifecycle | Steps 6–8 | `linux/packaging/arch/PKGBUILD`, systemd user unit, install/uninstall docs | Package starts only user-scoped services; uninstall ends its session and retains unresolved recovery evidence; no passwordless sudoers or system logind edits. User service restart policy cannot reacquire inhibition blindly. |
| 10. Supervised desktop validation | Built package, disposable workloads, recoverable session | `linux/docs/validation.md` | Record package commit, hardware, desktop and versions. Check expiry, extension, lock screen, close/open lid, AC/battery transition, UI/controller crash, logind restart and uninstall. Mark unsupported capabilities and any checks not run. |
| 11. Expand desktop coverage | Successful Hyprland core | GNOME and KDE capability adapters plus validation records | Repeat awake/lid/lock tests; ensure desktop power-manager lid policy is handled correctly. Avoid claiming Linux-wide support based on one desktop. |

## Recovery and deadline rules

Use a headless controller as the sole session/FD owner. It must release its own inhibition before exiting. Kernel FD cleanup provides crash fallback for that lock, but does not enforce deadlines in a still-alive hung controller; add a supervised lifetime/watchdog strategy and test it before unattended-use claims.

In the first release, avoid persistent display/audio/freeze effects, reducing recovery requirements. If later integrations mutate them, require an independent recovery service/timer, a shared journal lock, per-effect ownership, boot identity, and confirmation before clearing entries.

An application crash must not silently restart a formerly active session. On startup, reconcile timestamps, boot identity, capability availability, and residual journal entries. Explicitly define clock-jump behavior. Live duration uses a suitable Linux clock, and resume handling checks the persisted deadline before any reacquisition.

Timer completion releases this application's locks. Other applications may still inhibit suspend; normal desktop settings decide what happens next. Closed-lid release may lead immediately to sleep, so finish required cleanup before releasing lid inhibition.

## Later feature gates

1. **Display/keyboard:** identify only intended devices; record prior state; verify whether the compositor already handles the lid. Prove restoration after restart, reconnect, and a later manual brightness change.
2. **Audio:** restore the original device, not whichever device became default. Handle hotplug and manual changes; keep failed restoration visible.
3. **Thermal:** enumerate supported sensors/trip points, define device-specific thresholds and unknown states. Battery temperature is not CPU temperature. Do not claim safety from a universal guessed threshold.
4. **Freezing:** only explicitly selected isolated user units or controlled process groups. Never stop a compositor, shell, agent host, session services, database or container daemon by broad name matching. Revalidate identity; consider Linux pidfds for signal targeting.
5. **Network/tmux:** use saved NetworkManager profiles; offline behavior must preserve cancellation and avoid repeated keystrokes. Test tmux only on a private server with disposable panes.
6. **Browser relaunch:** verify supported flags and preserve profile arguments; explicit user action remains necessary because relaunch closes browser processes.

## Failure handling and stopping condition

Keep the macOS snapshot intact and develop the Linux target independently. Each adapter should fail visibly and leave unsupported optional features disabled. Preserve journals on parse/permission errors and failed restoration; do not continue with guessed ownership.

Research is complete when findings, sources, inventory and unresolved checks are saved. A full clone remains incomplete until shell networking works.

The first implementation milestone is complete only when the native package builds, rule/fake-bus tests pass, and a supervised target-desktop session proves expiry/crash cleanup and lid behavior. Cross-desktop support and optional-feature parity are separate milestones.
