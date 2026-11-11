# EverEye

A native Qt6 desktop app for timed awake sessions. Set a duration, keep your work running with the lid closed, extend the timer, and end the session from the window, tray, or command line.

EverEye is the renamed Linux product, with a dashboard based on the supplied reference: sidebar navigation, a circular countdown, cyan/purple actions, and live protection cards. Overview, Sessions, Lid actions and Preferences are functional pages, with compact navigation and scrolling on small screens.

**Public beta preparation — 0.4.0:** A previous native x86_64 matrix completed Ubuntu, Debian, Rocky, Arch, and Alpine build, regression, private-bus, desktop launch, and package lifecycle checks. The exact current release revision needs a fresh matrix after the Fedora/openSUSE package-gate fixes. ARM64, AppImage, Flatpak, and the physical 0.4.0 closed-lid check remain separate beta gates; this is not an all-distro compatibility claim. See [the beta release notes](docs/BETA-RELEASE.md), [the portability plan](docs/PORTABILITY-PLAN.md), and [current results](docs/PORTABILITY-RESULTS.md).

## Run the built application

From this directory:

```bash
./build/evereye
```

After rebuilding, use `./build/evereye --replace-ui` to replace an existing old window while preserving the session. To load controller fixes too, first run `./build/evereye shutdown` (ends any session), then reopen with `--replace-ui`.

Use Sessions for custom durations, Lid actions for lid policy, and Preferences for battery thresholds and diagnostics. In an active session, choosing an amount in Extend session immediately adds that time. The large switch starts or ends a session. The window uses responsive cards, readable fields, duration presets and a scrollable content area. Mouse wheel, trackpad, Page Up and Page Down reach content in short windows. [Review findings and checks](docs/UI-AND-BUGFIX-REVIEW.md).

Or extract the native archive in `dist/` and run `./evereye` inside the extracted directory. The archive dynamically links your system Qt libraries; it is not an AppImage or a universal Linux binary. The supplied binary targets the build machine's architecture and Qt version. Build from source when your distribution ships a different Qt ABI.

To inspect the interface without changing power settings:

```bash
./build/evereye --demo
```

## What works in this implementation

- Bounded sessions from one minute to 30 days; extend or end early.
- A native window and tray menu, with a CLI when no tray host is available.
- Separate per-user controller; closing the UI leaves a running session alone.
- logind sleep inhibition, with an optional low-level lid-switch inhibitor. No permanent logind.conf or sudoers change.
- Battery cutoff (default below 10%), unreadable-battery handling, and optional power-saver holds below 40% or with a closed lid.
- Atomic private journal, controller lock, explicit interrupted-session recovery, and a separate deadline/heartbeat watchdog.
- Visible errors, notifications, diagnostics, and screen locking left available.

These are implemented paths; the validation document distinguishes test evidence from unverified desktop integration. GNOME, KDE, Hyprland, and machine firmware can handle lid policy differently. A granted logind inhibitor alone is not proof of closed-lid behavior on every desktop.

This edition does not yet include the macOS app's app freezing, Docker Desktop freezing, display/keyboard dimming, audio mute/restore, hotspot switching, browser relaunch, or tmux automation. Linux thermal rules are also not implemented; the app does not claim temperature protection. Original source and the detailed porting research are in the parent workspace.

## Dependencies and build

Required: Linux with either pidfd supervision or CLOCK_BOOTTIME POSIX kernel timers, a C++20 compiler, Qt6 6.2+ Core/Gui/Widgets/DBus/Concurrent development files, qmake6, GNU make, a logind-compatible service (systemd-logind or elogind), a user D-Bus session, and UPower for the default battery protection. Tests additionally need Qt6 Test. Optional power saving needs a compatible power-profiles-daemon API. A tray host is optional because the window and CLI remain available.

On Arch/Omarchy, development packages are `base-devel`, `qt6-base`, and `upower`; `power-profiles-daemon` is optional. Install missing packages using your normal package manager. The app does not install or replace power-management services.

```bash
./scripts/build.sh
./scripts/test.sh
```

CMake now supports optional tests, prefix-aware staging, native DEB/RPM/TGZ generation and private-bus CTest cases. CMake is absent in this sandbox, so that route remains a required matrix gate; qmake is the locally verified build route. Use `-DBUILD_TESTING=OFF` for a runtime-only build and `-DEVEREYE_INSTALL_USER_SERVICE=OFF` on non-systemd or sandboxed installations.

```bash
cmake -S . -B build-cmake
cmake --build build-cmake
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ctest --test-dir build-cmake --output-on-failure
```

## Command line

```bash
./build/evereye doctor
./build/evereye start 2h30m
./build/evereye status
./build/evereye extend 15m
./build/evereye end
./build/evereye shutdown
```

`start`, `status`, `extend`, and `end` launch the controller if necessary. `shutdown` ends the session and exits the controller. `doctor` checks system services directly and returns nonzero when logind/system D-Bus is unavailable. All CLI responses except help/version are JSON; failed operations return a nonzero exit code.

## AI sessions and the phone gateway

```bash
./build/evereye run -- claude          # run an agent in a controller-owned PTY
./build/evereye run claude --name agent # start a preset from tasks.json
./build/evereye jobs                   # list jobs and exit codes
./build/evereye jobs log <id> -f       # follow live output
./build/evereye jobs send <id> "ls"    # type into a running job
./build/evereye jobs stop <id>         # SIGTERM, then SIGKILL after 5s
./build/evereye tasks                  # list presets and whether arbitrary commands are allowed
./build/evereye gateway [--bind 127.0.0.1] [--port 8737]
```

Jobs write `${XDG_STATE_HOME}/insomnia-linux/jobs/<id>.log` (0600) and are journalled in `jobs.json`; a job left running across a controller restart is marked `Interrupted`, never silently reattached. Presets live in `~/.config/evereye/tasks.json`, limits in `~/.config/evereye/jobs.json`.

The gateway is a separate opt-in process. It requires `Authorization: Bearer <token>` from `~/.config/evereye/gateway.json`, binds loopback by default, and refuses an off-loopback bind without TLS unless `EVEREYE_INSECURE_ALLOW_PLAINTEXT=1`. It serves the built PWA from `~/.local/share/evereye/web` (or `/usr/share/evereye/web`) and can POST job-end webhooks to ntfy/Telegram/Discord. Test it with `dbus-run-session -- python3 scripts/test-gateway.py`. Build the phone app with `cd ../mobile && npm ci && npm run build`. See [AI-SESSIONS.md](docs/AI-SESSIONS.md) for the API and security model.

Start options:

```bash
./build/evereye start 1h --no-lid --no-power-saver
./build/evereye start 1h --lid
./build/evereye start 1h --battery-cutoff 15 --power-saver-below 45
```

`--battery-cutoff 0` explicitly disables battery protection; otherwise unavailable battery/AC data prevents starting or ends an existing session after two consecutive failed polls. No system battery is a separate valid desktop state. With multiple batteries, the lowest valid system-battery percentage controls cutoff. Battery observations older than 180 seconds are treated as unavailable while on battery. Polling runs every 15 seconds.

When a user manually changes power profile, the app does not immediately reacquire the canceled hold. It waits until the requesting condition has cleared and occurs again. A missing optional profile provider leaves the awake session running with a visible warning.

## Session and recovery behavior

The controller owns the logind file descriptor. Session expiry releases this app's sleep/lid inhibition; it does not force a global suspend, and other applications may still inhibit sleep. On newer systemd versions a blocking sleep inhibitor can also affect manually requested suspend. Screen-lock idle inhibition is not requested.

Live countdowns use Linux `CLOCK_BOOTTIME`, which includes suspend time and does not follow wall-clock changes. The preferred watchdog runs in another process and targets its parent with a pidfd. Where pidfds are unavailable, a per-process CLOCK_BOOTTIME kernel timer delivers an unmaskable SIGKILL to the controller itself. Both enforce a ten-second heartbeat limit and the session deadline plus a two-second cleanup allowance, without signaling a reused PID. `doctor` reports the selected backend; `EVEREYE_SUPERVISOR=kernel-timer` forces the fallback for verification. Normal expiry is handled by the controller's one-second timer. Every extension must be acknowledged by the watchdog.

A controller crash releases descriptors through the kernel. On restart, an interrupted journal is marked inactive; the app never silently reacquires the old session. No arbitrary apps or global brightness/audio settings are modified in this release. Profile holds also disappear when the owning D-Bus client disconnects.

For compatibility with existing sessions, the internal D-Bus name remains `io.github.ProfBilal.Insomnia1` and the existing journal directory is retained. The private state file is `${XDG_STATE_HOME:-~/.local/state}/insomnia-linux/session.json`, with mode 0600 in a mode-0700 directory. `controller.lock` is held with flock. These paths are per user. A malformed journal is preserved and blocks new sessions; inspect it before moving it aside, then restart the controller. The app never deletes corrupted evidence automatically. Operational diagnostics print to the foreground daemon's stderr or the user service journal; SSIDs/passwords are not collected.

If profile release fails, the warning and owned cookie remain and the controller retries on later polls. `shutdown` disconnects the controller completely, which releases its remaining D-Bus-owned holds. Ending a session always releases sleep inhibition even if journal persistence fails.

## Tests and package

```bash
./scripts/test.sh
./scripts/test-dbus.sh
python3 scripts/package.py
```

The first command runs the test suite with a headless Qt platform and writes text/JUnit results plus UI screenshots to `test-results/`. It uses temporary journals and injected power services; process tests target only disposable child processes.

The second command uses a private D-Bus with fake logind/UPower/profile services and real Unix FD passing. It runs current and legacy profile cases (about 150 seconds total), each including a real 60-second expiry, and never points at your live system bus. It requires permission to create a local Unix socket. Failure to create that socket is a failed environment prerequisite, not a passing integration test.

The package command creates native/source archives, `SHA256SUMS`, and a local Arch `PKGBUILD` with the source digest filled in. To build the Arch package, run `makepkg` in `dist/` after reviewing the generated PKGBUILD. The packaging template itself is not ready until its digest marker has been filled by the package command. No package is uploaded or installed automatically.

Archive timestamps, ownership and modes are normalized so unchanged payloads yield the same checksums. Set `SOURCE_DATE_EPOCH` to a release timestamp if desired; it defaults to 0. This does not make binaries compiled with different toolchains identical.

The workspace build keeps `build/insomnia-linux` as a compatibility symlink to `build/evereye`. New packages and commands use EverEye. Older archives in `dist/` retain their original names and versions.

For a system package, the controller unit is available as `evereye.service`. Enabling it is optional: the app can launch its controller directly. Do not run both copies from different binary versions. Before removing the app, run `evereye shutdown`; normal state can be retained for settings/history. The journal is not a macOS-compatible migration format.

To install the verified build for your user without using a terminal afterward, run `./scripts/install-user.sh`. It copies the binary to `~/.local/bin`, installs the application-menu launcher and icon under `~/.local/share`, and does not require root or change global power settings. Remove it with `./scripts/uninstall-user.sh`.

See [architecture](docs/ARCHITECTURE.md), [validation and hardware checklist](docs/VALIDATION.md), and [third-party notices](THIRD_PARTY_NOTICES.md).

For a short supervised lid test on your own machine, run `./scripts/test-lid.sh`. It starts a one-minute session with lid inhibition and power saver disabled if needed, measures execution gaps for 40 seconds, prints progress and the result, and saves `test-results/lid-check.json`. Close the lid for about 20 seconds during the measurement. The script preserves an already-active session that has enough time remaining and never ends it early.

## Cross-distribution builds and E2E

Read [the plan](docs/PORTABILITY-PLAN.md), [research](docs/PORTABILITY-RESEARCH.md), and [results](docs/PORTABILITY-RESULTS.md) before treating an artifact as portable.

```bash
# One normal host: build, stage, regression, both private APIs, X11/Wayland
python3 scripts/test-e2e.py
# Clean native-architecture distro containers and package install/remove checks
python3 scripts/test-distros.py
# First portability targets: older Qt and musl
python3 scripts/test-distros.py --targets ubuntu-22.04,alpine-musl
# Continue an interrupted output directory without rebuilding passed targets
python3 scripts/test-distros.py --output test-results/distro-matrix-next --resume
# Audit the evidence before calling a build release-ready
python3 scripts/audit-portability.py
# Build formats on the appropriate target host (no host app installation)
./scripts/package-native.sh DEB
./scripts/package-native.sh RPM
./scripts/build-flatpak.sh
```

The E2E runner requires CMake or Qt6 qmake, test dependencies, `dbus-run-session`, `gdbus`, Xvfb/xvfb-run, Weston and AppStream tools. It fails with an incomplete JSON report if mandatory gates are missing or skipped. The distro runner needs a working Docker daemon and internet access; it installs/removes packages only in disposable containers. It records source/binary fingerprints, image IDs, failures and logs. Native arm64 runs are separate from x86_64; emulation is not accepted as physical/kernel evidence.

The Flatpak manifest is under `packaging/flatpak/`; preinstall its declared KDE Platform/SDK runtime before building. It grants narrow D-Bus service names for the existing feature set, not unrestricted system-bus access. The AppImage script needs verified linuxdeploy/Qt-plugin executables and an older glibc build host (at most 2.35); current-Arch builds are deliberately rejected as portable payloads. `packaging/nix/default.nix` provides a local-source derivation. These recipes are implemented but remain unverified until their real build/runtime gates pass. Gentoo and other source distributions can use the CMake route with local Qt6 and logind-compatible services.

AppImages start their detached controller through the original image to give it its own mount lifetime. Native archives remain dynamically linked to the build machine and are labeled accordingly in `build-info.json`.

After building the universal artifacts, exercise the actual bundles (supply the compiled current/legacy fixture paths):

```bash
python3 scripts/test-appimage.py dist/appimage/EverEye-x86_64.AppImage \
  --fixtures build-fake-services-current/insomnia-fake-services \
  --legacy-fixtures build-fake-services-legacy/insomnia-fake-services-legacy
python3 scripts/test-flatpak.py dist/flatpak/EverEye-x86_64.flatpak \
  --fixtures build-fake-services-current/insomnia-fake-services \
  --legacy-fixtures build-fake-services-legacy/insomnia-fake-services-legacy
```

Use `scripts/private-bus-run.py` for fixture protocol tests; the integration script now refuses direct/live-session-bus invocation. Universal runtime tests retain their own JSON reports. Flatpak tests use an isolated installation and temporary fixture-state grants; they do not replace the normal user-installed app or delete its data. AppImage tests verify Qt libraries load from the extracted bundle, then test the original image's controller lifetime.
