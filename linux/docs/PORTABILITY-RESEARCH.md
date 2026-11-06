# Portability research ledger

Accessed 2026-11-06. Question: how can the existing native Qt/D-Bus app be distributed broadly without losing bounded-session behavior or overstating validation?

| Primary source | Relevant contract and decision |
|---|---|
| [Qt Linux support](https://doc.qt.io/qt-6/linux.html) | Qt runs across multiple Linux families, but binary ABI/glibc baselines still matter, particularly aarch64 vendor binaries. Source support is not proof a host-built Qt binary runs elsewhere. Build native packages on their target distro; build AppImages on an explicit older baseline. |
| [AppImage best practices](https://docs.appimage.org/reference/best-practices.html) | Bundle non-base dependencies, build on an old enough system, and actually test intended targets. A tarball of this Arch binary is not a universal release. |
| [AppImage environment](https://docs.appimage.org/packaging-guide/environment-variables.html) | APPIMAGE identifies the original image; APPDIR identifies its mounted content. Inference: a detached controller must launch its own image runtime instead of depending on the UI mount lifetime. |
| [linuxdeploy Qt plugin](https://github.com/linuxdeploy/linuxdeploy-plugin-qt) | Supports Qt 5 and 6 and deploys required plugins/resources. Use it with linuxdeploy, including Wayland/XCB plugins; do not invent a partial Qt bundle. |
| [Flatpak permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html) | D-Bus access is restricted by default. Give only required system service names and established app-owned user names; no unrestricted bus/filesystem/network access. Runtime permissions must be tested, not just declared. |
| [logind D-Bus API](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html) | Owned inhibitor descriptors implement sleep/lid inhibition. Provider access and desktop/lid behavior are separate contracts. |
| [elogind upstream](https://github.com/elogind/elogind) | Standalone logind service for non-systemd systems. Source code uses the compatible login1 D-Bus protocol, so build/runtime packages must not require systemd as PID 1. Actual elogind behavior remains a matrix gate. |
| [PPD current API](https://upower.pages.freedesktop.org/power-profiles-daemon/gdbus-org.freedesktop.UPower.PowerProfiles.html) | Lease holds are canceled on explicit release, client exit or manual profile change. Bind to the owning service and preserve manual override behavior. |
| [PPD upstream](https://gitlab.freedesktop.org/upower/power-profiles-daemon) | Current name and historic `net.hadess.PowerProfiles` coexist across distro generations. Implement independent discovery of both names/paths/interfaces; verify both using private fixtures. The old raw tag URL was inaccessible to the research tool, so do not claim that raw file was inspected. |

## Choices and unresolved evidence

- Flatpak is the preferred common-runtime route for glibc/musl/NixOS desktop families where Flatpak is available. Its D-Bus permissions, pidfd policy and detached process behavior are release gates.
- AppImage is a second portable route for suitable glibc systems. The proposed build floor is Ubuntu 22.04/glibc 2.35 and Qt 6.2; older glibc targets use Flatpak/native/source until an older payload is actually built/tested. This is an explicit artifact limitation, not a claim that every distro must use AppImage.
- Native DEB/RPM/Arch and source/Nix routes cover integration and environments without the universal runtimes. Alpine uses its own musl toolchain; never reuse a glibc ELF there.
- Linux pidfd supervision is retained as the preferred process backend. For older or restricted kernels, a kernel-owned POSIX timer can enforce the same deadline/heartbeat using SIGEV_SIGNAL/SIGKILL without choosing another process by a numeric PID. This is a real independent kernel mechanism, not a racy PID fallback. Both backends need actual process tests.
- Flatpak's portable suspend portal alone does not promise low-level lid-switch inhibition. Keep the narrow direct logind protocol for the implemented feature; a future no-logind fallback needs explicit reduced lid capability and separate behavior tests.
- Container tests prove package and protocol behavior on those userspaces, not physical lid/firmware or a different host kernel. X11/Wayland and supervised hardware evidence are separate gates.

## Older-kernel supervisor extension

[Linux timer_create](https://man7.org/linux/man-pages/man2/timer_create.2.html) documents initially disarmed per-process timers and suspend-aware CLOCK_BOOTTIME (since 2.6.39). [timer_settime](https://man7.org/linux/man-pages/man2/timer_settime.2.html) documents absolute arming and disarming. Inference applied in EverEye: a kernel-generated SIGKILL to the owning controller enforces the earlier of its next heartbeat limit and final deadline, with no PID-reuse race and no dependency on its event loop. The timer is disarmed/deleted before optional cleanup; actual expiry/hang/extension/stop tests are required. Link librt for pre-2.34 glibc as well as current libc implementations.
