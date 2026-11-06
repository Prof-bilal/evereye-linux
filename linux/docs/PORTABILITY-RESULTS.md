# Cross-distribution readiness results

State: **in progress; not release-ready across all Linux distributions**. Updated 2026-11-06 for the working 0.4.0 sources. This is a requirement audit, not a replacement of the user's objective with local-only support.

| Gate | Authoritative evidence | Current result |
|---|---|---|
| Plan before implementation | `PORTABILITY-PLAN.md` | Written |
| Primary-source portability research | `PORTABILITY-RESEARCH.md` | Written; identified ABI, runtime and service limits |
| Local app/source build | `test-results/portability-local/build-app.log` and `version.log` | Pass, Qt 6.12/GCC 16 on x86_64 Omarchy |
| Regression/process/UI behavior | `test-results/distro-matrix/{ubuntu-22.04,alpine-musl}/e2e/regression.txt`, JUnit XML; local `test-results/tests.txt` | 73 passed, 0 failed/skipped on both distro userspaces and locally |
| Current and legacy profile fixtures | Both distro E2E reports and private-bus logs | Real transport passes on Ubuntu and Alpine, including the forced kernel-timer backend |
| Staged installation and real staged executable | `install-stage.log`, installed files/version/render stages | Pass using qmake; launcher, icon, metadata and optional unit staged |
| Desktop-entry validation | `desktop-metadata.log` | Pass |
| AppStream metadata validation | Both distro E2E reports | Pass on Ubuntu and Alpine |
| CMake and Qt baselines | Ubuntu 22.04 and 24.04 E2E reports, configure/build logs | Pass on Qt 6.2.4 (22.04) and the 24.04 Qt baseline |
| Real current/legacy private D-Bus E2E | Both distro E2E reports | Pass, including FD transport, crash release, recovery and real expiry |
| X11/Wayland application launch | Ubuntu and distro E2E reports | Ubuntu, Fedora, Rocky, Arch and Alpine pass both. openSUSE now reaches Wayland; its X11/AppStream checks require the latest metadata/runner fixes and rerun |
| DEB/RPM/Arch/TGZ package installation/protocol/removal | Matrix package reports | Ubuntu 22.04/24.04, Debian 12/13 DEB, Rocky RPM, Arch package and Alpine TGZ gates pass. Fedora RPM now passes after the isolated unsigned-package and timing fixes. openSUSE RPM needs the latest shared-license-directory fix and rerun |
| Debian/Ubuntu/Fedora/RHEL/Arch/openSUSE/Alpine userspaces | `ci/distro-matrix.json`, `scripts/test-distros.py` | Ubuntu 22.04 and 24.04 native gates pass. Alpine is incomplete only at desktop gates; remaining configured targets unverified |
| Native x86_64 and arm64 | CI matrix with native hosted runners | Workflow prepared, not published or run |
| Flatpak build/runtime/permissions/lifecycle | Manifest and build script | Implemented build path; runtime tests still required |
| AppImage old-baseline build and clean-host runtime | AppImage build script, controller launch regression | Build path and child-launch fix implemented; actual image/runtime still required |
| NixOS/source-family build | Local Nix derivation and CMake instructions | Derivation supplied; Nix evaluation/build not run |
| Physical logind/elogind, lid, desktop policy | Target-machine records | Historic 0.1.0 Omarchy result only; not a 0.4.0 claim |

## Environment and current reports

The execution sandbox denies creating Unix sockets, denies Docker's API socket and cannot resolve external hosts from shell commands. It has no CMake, Flatpak builders, AppImage tools or nested desktop servers. Research browsing works. The E2E runner records mandatory prerequisites as blocked and returns nonzero rather than treating this as a passing end-to-end release.

The current local report is `test-results/portability-local/report.json` with `complete: false`. The first local run predates adding source-fingerprint fields to the runner; its compile/test/install logs remain valid local evidence, but should not be used as a fully fingerprinted matrix result.

The first normal-terminal two-distro run finished at 2026-11-06 04:36:52 UTC. Its report is incomplete: Ubuntu 22.04 is complete, Alpine is not. A later resumable run in `test-results/distro-matrix-next/report.json` completed Ubuntu 22.04 and 24.04; Ubuntu 26.04 was interrupted during its image download. These reports belong to frozen source snapshots; later packaging/runner fixes require their own verification. All containers share the Omarchy host kernel, so their results do not prove older-kernel or physical machine behavior.

Alpine's `weston.log` identifies the exact missing `/usr/lib/libweston-14/headless-backend.so` module. Its recipe now explicitly includes `weston-backend-headless`, `weston-shell-desktop` and `xvfb-run`. See Alpine's [Weston package split](https://pkgs.alpinelinux.org/package/v3.23/community/x86_64/weston) and [Xvfb wrapper package](https://pkgs.alpinelinux.org/package/edge/community/x86_64/xvfb-run). This correction has not yet been rerun in Docker; the old report remains unchanged.

A concrete external command has been provided for the first target builds:

```bash
python3 /home/abullah/Projects/OpenEye/linux/scripts/test-distros.py --targets ubuntu-22.04,alpine-musl
```

The full matrix uses the same command without `--targets`. Reports must be inspected and failed implementations repaired. Preparing recipes/workflows is not evidence their outputs run. The active goal remains open until the required end-to-end scope has actual evidence.

For the next run, preserve the original evidence in its existing directory:

```bash
python3 /home/abullah/Projects/OpenEye/linux/scripts/test-distros.py \
  --output /home/abullah/Projects/OpenEye/linux/test-results/distro-matrix-next
```

The runner now prints the image/E2E/package stage and log path. Package logs print each lifecycle step. E2E reports and compositor logs are copied out before the additional package tests, so diagnostics are available without waiting for the full target to finish.

If a run is interrupted, continue it without rebuilding completed targets:

```bash
python3 /home/abullah/Projects/OpenEye/linux/scripts/test-distros.py \
  --output /home/abullah/Projects/OpenEye/linux/test-results/distro-matrix-next \
  --resume
```

Resume validates the source fingerprint and native architecture before reusing a completed target. If either changed, it rebuilds that target rather than presenting stale evidence as current.

Before release review, run `python3 scripts/audit-portability.py`. It requires complete native amd64 distro evidence, a native arm64 report, AppImage and Flatpak runtime reports, and a 0.4.0 physical-lid report. Missing evidence remains incomplete and returns nonzero; the audit never converts skipped or blocked stages into passes.

## Additional checks and runtime runners

- All Python runner sources and JSON manifests parse; shell scripts pass `bash -n`.
- The integration entry point refuses direct invocation without a fresh private-bus parent marker, before launching fixtures or touching sessions.
- Native package lifecycle tests refuse to install on the host; an explicit disposable Docker-container guard is required.
- The source archive excludes build/result/cache directories; packaging no longer accidentally includes the newly named fixture build directories.
- Source/native tar archives and the Arch source input normalize gzip/tar timestamps, ownership and permission modes. `SOURCE_DATE_EPOCH` is honored (default 0). Two actual packaging runs produced identical source/native/PKGBUILD checksums; an additional check varied source timestamps and umask-derived modes while preserving identical archive bytes. Evidence: `test-results/reproducible-packaging.json`. This verifies archive reproducibility for unchanged payloads, not reproducible compiler output or an external runtime gate.
- The Arch package lifecycle tester now derives the package version from `CMakeLists.txt` and rewrites the generated `pkgver`, preventing a future version bump from producing a mismatched source directory or package filename. Python compilation and shell syntax checks pass after this change.
- `evereye doctor` correctly exits nonzero for this unavailable system bus, while reporting the actual Qt/kernel/architecture and available pidfd supervision.
- `scripts/test-appimage.py` now exercises an actual image's extraction, bundled Qt library origin, rendering and both private-bus/lifetime flows. It has not run without an image/tools/build baseline.
- `scripts/test-flatpak.py` now installs a bundle in an isolated Flatpak user installation, exercises rendering and current/legacy private service flows, and removes that test installation's app. Fixture journals receive explicit temporary-directory grants; production permissions are unchanged. This runner still requires actual runtime execution.
- The integration crash case now targets the actual controller PID from its private bus, so outer AppImage/Flatpak launcher processes cannot hide a leaked controller.

These runner improvements do not change pending transport/runtime/matrix gates to passed. Current recipe/package availability, nested compositor options, Flatpak isolation and AppImage bundling must be repaired against real results if the target environments expose differences.
