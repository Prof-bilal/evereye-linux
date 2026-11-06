# Linux architecture

## Processes and trust boundaries

`evereye` starts the native window/tray. `--daemon` starts a headless controller owning the user-session D-Bus name `io.github.ProfBilal.Insomnia1`. The UI and CLI call `Request(action, json)` at `/io/github/ProfBilal/Insomnia`, interface `io.github.ProfBilal.Insomnia.Controller1`. The controller checks the caller's Unix UID. Only the explicitly scriptable request slot is exported. `Changed` announces state updates; the UI also polls once per second. UI requests run on a QtConcurrent worker so controller or service latency cannot block scrolling. One request runs at a time; a user action can queue behind a status read, and mutating requests are never automatically repeated.

The controller calls the **system** D-Bus for logind, UPower, and power profiles. Its owned inhibitor FD has `FD_CLOEXEC`. The independent `--watchdog` child receives deadlines through a pipe, acknowledges them, and uses a pidfd for race-free termination of its own parent. It inherits no inhibitor across exec. A controller cannot start a session without an acknowledged watchdog deadline.

No root helper, shell-command execution, sudoers rule, permanent lid configuration, or external web service is involved. The private bus fixture exists only in the tests target. Production service names and object paths are fixed; the test runner overrides the standard system-bus address with a private bus before starting the production binary.

## Data and operations

The core Controller accepts `start`, `extend`, `end`, `status`, and `doctor`. The service additionally accepts `shutdown`. Mutating operations run synchronously on one event loop, so overlapping requests are serialized. `start` and `extend` validate numeric bounds and types. Unknown options are refused. Mutating operations are not automatically retried by clients after a timeout because the outcome may be ambiguous; clients should read status first.

DiskJournal takes an exclusive flock for the controller lifetime and writes schema-1 JSON through QSaveFile with no direct-write fallback. File and directory data are fsynced. Final state/lock symlinks, incorrect owners, nonregular files, and unsafe lock hardlinks are refused. State parents can still be managed by the same user; this is not a boundary against a compromised same-user account.

Start ordering is battery validation, session journal, watchdog acknowledgment, then logind inhibition. The UI reports active only after successful acquisition. Failures release the descriptor and supervisor and record an inactive result. Extension writes the new durable deadline, then requires supervisor acknowledgment; a supervisor failure ends the session rather than continuing with conflicting deadlines.

End releases sleep/lid inhibition and stops the watchdog before attempting profile release and inactive persistence. Optional service latency cannot delay removal of the sleep inhibitor. Failed persistence blocks another start until reconciliation. Failed profile release keeps the owned cookie and retries during both active and inactive sessions; a recovered release clears its warning. On daemon shutdown, its bus disconnect releases remaining leases. Startup never resumes an active journal automatically.

## Time, power, and capability limits

The one-second deadline timer uses CLOCK_BOOTTIME. Persisted UTC time is descriptive, not an alternate live clock. The watchdog bounds controller hangs and normal timer delays; a two-second deadline allowance permits cleanup before forced termination. Its heartbeat timeout is ten seconds.

UPower is polled every 15 seconds. State distinguishes missing battery, unreadable battery, and known percentage. System batteries are selected by Type/PowerSupply/IsPresent, excluding peripheral batteries. Unknown AC is treated conservatively when protection is enabled. Two consecutive failed observations end a protected session. Power saver requests are lease-based and respect cancellation by a manual profile change.

Blocking D-Bus calls have bounded per-call deadlines. A service that hangs long enough also triggers the independent supervisor. Capability checks cannot prove firmware/compositor behavior; closed-lid and screen-lock verification requires the actual desktop. Thermal, arbitrary process freezing, network switching, and persistent brightness/audio changes are outside the implemented release.

## Source map

| File | Responsibility |
|---|---|
| `src/core.*` | Rules, validation, lifecycle, state, journal, lock |
| `src/platform.*` | logind, UPower, power profiles, notifications |
| `src/guardian.*` | Acknowledged watchdog pipe and pidfd enforcement |
| `src/service.*` | User D-Bus transport, caller ownership, controller lifecycle |
| `src/window.*` | Native controls, tray, accessible field labels, error UI |
| `src/main.cpp` | GUI/CLI/daemon/watchdog entry points and offline preview |
| `tests/test_insomnia.cpp` | Injected failures, real process watchdog, disk/UI tests |
| `tests/fake_services.cpp`, `tests/integration.py` | Real private-bus transport and descriptor tests |

## Window layout

The header and power-status footer remain visible. Session and rule cards share the width above 850 logical pixels and stack below that width. A QScrollArea owns the content, with wheel, trackpad and keyboard scrolling. Number fields forward wheel events to the scroll viewport to avoid accidental timer edits. Their editor geometry is set explicitly to work around the one-pixel edit field produced by the installed Qt stylesheet style with NoButtons.

`--replace-ui` targets only the registered UI process, checking its bus-owner UID, executable name, and stable owner before using a pidfd. It leaves the daemon and its session running. Loading controller fixes also requires ending the session and shutting down the old daemon before reopening.

## EverEye 0.4.0 branding and navigation

The executable, desktop entry, icon and optional systemd unit now use EverEye. Existing controller D-Bus names, interface/object paths and the private `insomnia-linux` state directory are intentionally preserved, so a product rename does not create a second controller or abandon an active journal. The workspace build provides an old-name executable symlink.

The dashboard is an Overview page plus Sessions, Lid actions and Preferences in a stacked layout. Wide windows show the reference sidebar; below 850 logical pixels a keyboard-accessible navigation picker replaces it. Below 690 pixels the timer/actions and protection cards stack. The common scroll area also contains errors and reconnect controls, which remain available on every page.

The large dashboard switch starts/ends a session. The overview action button starts the selected duration or ends a running session. The active extension picker applies the chosen amount immediately; Custom duration opens Sessions. All displayed protection summaries derive from actual settings or controller state. Recovery means releasing interrupted sessions, not automatically restarting work.

## Supervisor portability

Pidfd process supervision remains preferred. If unsupported, ProcessGuardian uses an owning-process POSIX CLOCK_BOOTTIME timer with SIGEV_SIGNAL/SIGKILL. Each heartbeat arms the kernel for the earlier of now + ten seconds and final deadline + two seconds; neither wall-clock changes nor a blocked Qt loop can extend it. Ending first releases the sleep inhibitor, then disarms/deletes the timer before optional service/persistence work. SIGKILL cannot be handled or masked. No other process is selected by a reusable PID. `doctor` reports the selected backend, and `EVEREYE_SUPERVISOR=kernel-timer` provides a reproducible forced fallback.
