# Validation status

Build under test: EverEye 0.4.0, local source in this directory. Validation started 2026-11-05 and continues 2026-11-06 (Asia/Karachi).

**Release decision: public-beta candidate for the observed Omarchy/Hyprland machine and the completed native x86_64 matrix targets.** EverEye 0.4.0 adds the supplied dashboard design, navigation pages, product branding, dual profile APIs and older-kernel supervisor fallback. The current full suite passed **74 checks**, zero failed/skipped, in about 105 seconds. Ubuntu 22.04/24.04/26.04, Debian 12/13, Rocky 9, Arch, and Alpine/musl completed the native x86_64 build, package, private-bus, and desktop checks. Fedora and openSUSE package/metadata reruns, ARM64, universal bundles, and a physical 0.4.0 lid check remain beta gates. This does not establish universal Linux compatibility or unattended thermal safety.

## Environment and verified checks

Local host reports Omarchy 4.0.6 / Arch, x86_64, Qt 6.12.0, GCC 16.2.1, systemd 262.1, UPower 1.91.5, power-profiles-daemon 0.30, and NetworkManager 1.58.1. The execution sandbox has a read-only home outside the workspace, rejects system/session D-Bus access, and rejects binding a private Unix socket. Tests use workspace outputs, temporary directories, offscreen Qt, and disposable child processes.

| Requirement | Evidence / command | Status |
|---|---|---|
| Native application builds | `scripts/build.sh`, `test-results/build.log` | Passed using qmake6 and GNU make |
| Duration bounds, strict types, extension ceiling | `tests/test_insomnia.cpp`, JUnit report | Covered by automated tests |
| Session lifecycle and transactional failures | Injected platform/guardian/journal tests | Covered by automated tests |
| Release actual owned descriptors | Pipe-backed test platform, fcntl after end | Passed; not a logind transport test |
| Independent expiry and hang supervisor | Actual watchdog/victim subprocess tests, pidfd targeting | Passed in the final automated run |
| Extension acknowledged by watchdog | Disposable child survives original supervised deadline then expires | Passed |
| Normal 60-second expiry keeps controller alive | `normalExpiryKeepsControllerAlive` | Passed in the final automated run |
| Battery cutoff and unknown/absent distinction | Core policy tests and UPower parser tests | Covered by automated tests |
| Power-saver manual override / retry | Injected profile tests and current/legacy private-bus E2E | Covered by automated tests and both private profile APIs; live desktop-provider behavior remains a host gate |
| Durable private state and exclusive ownership | Temporary real files, mode checks, corrupt/symlink/second-lock tests | Passed |
| Window start / extend / end and error state | Offscreen Qt mouse events and PNG captures | Passed; not a desktop tray-host test |
| Tiled and short-window layout | `tiledAndShortWindowsKeepOptionsUsable`; tall content scrolls instead of squeezing the options | Regression checks passed after user screenshot exposed clipping |
| Desktop entry and shell/Python syntax | desktop-file-validate, bash -n, py_compile | Passed |
| Private-bus logind/UPower/profile transport | `scripts/test-dbus.sh`, distro E2E reports, [user-terminal result](DBUS-INTEGRATION-RESULT.md) | Passed in the user's normal terminal and in Ubuntu 22.04/24.04 containers; sandbox execution remains blocked |
| Real logind and battery service access | `evereye doctor` | Blocked by sandbox; returns explicit failure |
| Arch package staging/source integrity | make install with workspace INSTALL_ROOT; SHA256SUMS; makepkg --verifysource | Passed; extracted native archive also executes and renders its Qt preview |
| systemd unit parser | systemd-analyze --user --man=no verify | Blocked by sandbox socket-option permissions; no successful static verification claimed |
| CMake route | Alternative supplied configuration | Not run: CMake unavailable |
| Physical closed-lid workload | User ran `scripts/test-lid.sh` after the lid-close instructions | Passed: largest execution pause 1.00 seconds across 40 seconds; see [live result](LIVE-DESKTOP-RESULT.md) |
| Live session expiry journal | Read-only inspection of actual app state after the lid test | Passed: inactive with reason Timer expired, lid enabled |
| Physical AC transition, tray host, screen lock | Checklist below | Not separately exercised; remaining compatibility/release checks |

The earlier 0.2.0 automated run completed **63 passed, zero failed, zero skipped**. The current 0.4.0 run supersedes those workspace result files; see [EverEye design and validation](EVEREYE-DESIGN.md). Text and JUnit evidence are in `test-results/tests.txt` and `test-results/tests.xml` in the build workspace. Private-bus transport is now covered by the Ubuntu 22.04/24.04 container reports; the live closed-lid workload and other physical compatibility checks remain separate.

## Reproduce automated checks

```bash
./scripts/build.sh
./scripts/test.sh
./scripts/test-dbus.sh
```

The test suite takes about 80 seconds with the normal 60-second timer test. The D-Bus integration runner takes a further 75 seconds. The latter creates its own bus and overrides DBUS_SYSTEM_BUS_ADDRESS only for its children, so production code talks to test fixtures instead of real power services.

Private-bus coverage includes detached-controller startup under captured CLI output, D-Bus argument/result transport, real FD return, duplicate start, extend/end, denied inhibition, profile holds and manual cancellation, low-battery start refusal, controller crash/FD closure, journal reconciliation, normal expiry, and shutdown.

## Supervised target-desktop checks

Use the actual built binary in a normal desktop terminal. Keep recoverable work on a ventilated surface and observe the short session. These steps change only this app's temporary inhibitor/profile holds. They are intentionally not executed automatically by the test suite.

1. Record `evereye --version`, Qt/systemd versions, distribution, desktop version, hardware model, and package/binary digest. Run `evereye doctor`; confirm logind and UPower access.
2. Run `evereye start 2m --no-power-saver`. Confirm `status` reports active and `systemd-inhibit --list` contains EverEye with the expected sleep/lid types. Verify screen lock remains usable.
3. Close the lid for 15–30 seconds, then reopen. Verify the laptop remained running using actual sleep/wake logs and a disposable timed workload. Do not infer this solely from the countdown or inhibitor list.
4. Extend with `evereye extend 1m`, then end. Confirm this app's inhibitor is gone. Other applications' inhibitors must remain unchanged.
5. Repeat a one-minute session through natural expiry. Confirm the daemon stays alive and this app's lock disappears within the normal tick interval. Keep the binary digest and observed timestamps.
6. With the UI closed, use CLI status to confirm the controller still runs; reopen the UI and verify its countdown matches. Test tray open/end actions if a tray host exists, and window-only behavior without one.
7. Using only a disposable test session, terminate the controller by its verified PID. Confirm its inhibitor/profile hold is gone and restarting reports interruption without reacquiring it. Do not kill processes by broad names or unrelated PIDs.
8. Observe AC unplug/replug on a sufficiently charged battery. Verify percentages and policy in the UI. Exercise cutoff and stale readings in private fixtures, not by draining the physical battery. If power saver is active, manually change profile and confirm Insomnia respects that choice.
9. Run `shutdown` before package removal. Confirm no app-owned inhibitor/hold remains. Keep unresolved journals intact.

Record pass/fail, exact steps/times, relevant journal output, and anything not run. Hyprland validation does not establish GNOME/KDE support; repeat the affected cases on each desktop before claiming it.

## Review and residual risk

The source review covered input types/bounds, D-Bus caller UID, explicit exported slots, no shell execution/root helper, private-file ownership and symlink checks, descriptor inheritance, clock/deadline rules, detached-child stdio, normal expiry versus forced watchdog expiry, profile release retries, and preserving corrupt evidence. This was a local review by the implementer, not an independent audit or certification.

The watchdog's private entry point verifies a pipe-based protocol and matching parent executable, so accidentally launching it from a terminal does not target the shell. No signal is sent before a valid initial deadline. The tests signal only disposable child processes they start themselves.

The user supplied a successful private D-Bus integration result and a successful short closed-lid workload result from their normal terminal on 2026-11-06. Broader physical compatibility checks remain as listed above. Thermal protection and the other deferred macOS integrations are explicitly absent; see the README. The native archive is a tested development artifact for the observed target environment.

The user also supplied a screenshot of the app running on the actual desktop, connected to the controller and reading 99% battery on AC. This proves live window/controller/power-read access in that environment. The screenshot revealed clipped options under tiling; version 0.2.0 replaces the form with responsive cards and was regression-tested at 1100×540, 380×500 and 360×340 logical pixels, including 2× scaling. See [the UI and bug-fix review](UI-AND-BUGFIX-REVIEW.md). The subsequent live lid test reported 1.00 seconds as its largest execution gap.
