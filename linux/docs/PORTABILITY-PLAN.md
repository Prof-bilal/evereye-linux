# EverEye cross-distribution readiness plan

Started 2026-11-06. Owner: implementation agent. User objective: make EverEye ready across Linux distributions, plan first, implement, and test end to end. This plan preserves that objective; generating packaging recipes alone does not complete it.

## Outcome and boundaries

Provide a broadly installable EverEye, with reproducible source/native/universal packaging, explicit capability discovery, and end-to-end evidence on representative distro families, CPU architectures and desktop backends. Preserve the existing UI and session safety. This work does not change users' system power configuration, install packages automatically, publish releases, or port the macOS-only deferred features.

“All Linux distros” cannot mean every historical kernel, libc, architecture or machine firmware. Coverage must include the maintained Debian/Ubuntu, Fedora/RHEL, Arch, openSUSE, Alpine/musl and NixOS families through appropriate package/source/runtime routes. Required OS facilities and limitations are part of the contract, not hidden assumptions. Untested targets remain release gates, not claimed successes.

## Verified starting evidence

- Native x86_64 Omarchy/Arch build, Qt 6.12, GCC 16; the current automated suite has 74 checks.
- UI uses Qt Core/Gui/Widgets/DBus/Concurrent, with a user controller, UPower, logind-compatible inhibition and optional PPD holds.
- Native artifacts currently link host Qt/glibc and are not universal bundles.
- PPD recognizes only the current interface; older `net.hadess.PowerProfiles` must work too.
- CMake unconditionally requires Test, even when tests are disabled; install paths are hardcoded.
- Starting runtime used pidfds for safe watchdog signaling. Work now adds an independently enforced POSIX kernel-timer backend for older/restricted kernels, plus explicit diagnostics if neither mechanism is available.
- Existing private D-Bus and hardware evidence belongs to 0.1.0, not this cross-distro release.
- The present execution sandbox denies Unix sockets/Docker and shell DNS. This is an execution constraint; the private-bus and distro-matrix gates remain mandatory.

## Ordered work and acceptance evidence

| Step | Files / output | Acceptance evidence |
|---|---|---|
| 1. Research platform contracts | `PORTABILITY-RESEARCH.md`, compatibility matrix | Primary sources for Qt, logind/elogind, PPD versions, AppImage and Flatpak; recorded date and limits |
| 2. Fix source/runtime portability | `src/`, CMake/qmake definitions | Older Qt baseline builds; dual PPD API/private-bus cases; usable capability diagnostics; correct AppImage child lifetime; UI replacement works across namespaces |
| 3. Make builds/install paths portable | CMake, build/test scripts, AppStream metadata | Out-of-tree build; tests optional; staged install honors prefix/libdir; launcher, icon, metadata validation |
| 4. Package usable distribution routes | Flatpak manifest, AppImage script, DEB/RPM/Arch recipes, source/Nix route | Real artifacts install/run/uninstall in isolated environments; runtime dependencies resolved; checksums and build provenance |
| 5. Add reproducible cross-distro E2E | Matrix runner, containers, CI workflow, unified E2E runner | Debian/Ubuntu including oldest Qt target, Fedora/RHEL, Arch, openSUSE and Alpine; native x86_64/aarch64 evidence; reports retain failures/skips |
| 6. Exercise desktop/runtime routes | X11/Wayland checks, Flatpak and AppImage tests | UI launch, start/extend/end, detached controller, duplicate UI/replacement, FD release, profile override, battery cutoff, crash/recovery, expiry, package removal |
| 7. Audit release readiness | `PORTABILITY-RESULTS.md` | Requirement-by-requirement current artifacts and actual results. No failed or missing mandatory gate labeled passed |

Steps 1–3 precede packaging. Independent package formats may be implemented after the build contract stabilizes. Local unit/static/render checks proceed while external matrix execution is unavailable. Failed implementations are revised and retested; original macOS source and user session journals remain untouched.

## Test layers and completion

1. Local unit/regression, malformed input, state ownership, real watchdog processes, responsive UI and alpha corners.
2. Private bus with fake logind/UPower and both current/legacy profile APIs, real FD passing and actual normal expiry.
3. Installed executable and package lifecycle in clean distro containers, not just compilation or extraction.
4. Universal-package runtime tests with correct D-Bus permissions and child-process lifecycle.
5. X11 and Wayland launch/interaction plus target hardware checks for lid behavior; container mocks cannot prove firmware behavior.

Finish only after implementations and their mandatory E2E gates have authoritative evidence. If execution needs a normal host/runner, provide a concrete runner and retain that gate as pending until its report is inspected. Do not change the goal to merely preparing scripts.

## Implementation refinement: older kernels

A per-process CLOCK_BOOTTIME POSIX timer with SIGEV_SIGNAL/SIGKILL is being added as the safe fallback when pidfds are unavailable. This broadens enterprise/older-kernel coverage while retaining bounded session behavior. It must pass real-process deadline, hang, extension, disarm and normal 60-second expiry checks, plus a forced-kernel private-bus lifecycle case. Numeric PID kill fallbacks remain forbidden.
