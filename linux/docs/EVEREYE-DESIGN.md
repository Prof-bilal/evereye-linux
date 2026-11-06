# EverEye 0.4.0 design and rename

Implemented 2026-11-06 from the user-supplied dashboard reference. This is the native Qt app, not a static mockup.

## Design and behavior

The wide layout follows the reference's rounded shell, dark sidebar, workspace breadcrumb, cyan/purple brand mark, status badge, large session switch, circular timer, extension/sleep cards, gradient session action and three safeguard cards. Fonts and rasterization vary with the desktop. The supplied screenshot is a concept reference; display text in the implementation reports actual app behavior.

Overview shows the current controller session. The switch starts or ends protection; the gradient action starts the chosen duration or ends a running session. While active, choosing an extension applies it immediately. Custom duration opens Sessions. The circular ring uses the observed remaining duration, and the end time is an estimate based on the current wall clock; the actual deadline remains CLOCK_BOOTTIME based.

Sessions provides days/hours/minutes, presets, start/extend and quick extensions. Lid actions configures the next session's lid policy. Preferences configures power saver, thresholds and diagnostics. Settings are locked while a session is running. The crash-recovery card describes release on interruption, not automatic resumption.

Wide windows show the sidebar. Below 850 logical pixels a keyboard-accessible navigation picker replaces it. Below 690 pixels the timer/actions and safeguard rows stack. The content scrolls vertically; errors and reconnect remain reachable on every page. Existing numeric-editor and wheel fixes are retained.

Loading, offline, ready, running, working, validation failure, provider warning and recovery-blocked states come from the controller/transport. A failed start resets the switch. Offline and recovery-blocked states disable start controls. The demo is isolated from power services; the screenshot mode intentionally simulates an active countdown and identifies itself as an offline preview.

## Rename and compatibility

The product title, sidebar, tray, CLI help/version, notification brand, icon, desktop launcher, optional systemd unit and generated archives/packages now use EverEye. The native command is `evereye`, version 0.4.0. The workspace build retains `build/insomnia-linux` as a compatibility symlink. New desktop files use `io.github.ProfBilal.EverEye`.

The D-Bus name, object/interface names and `insomnia-linux` journal directory are retained for existing-session compatibility. C++ namespace/test-fixture identifiers remain internal. Original macOS source, upstream URLs, license attribution and historic validation records retain their original names. Nothing was installed and no user desktop configuration was changed.

Run `build/evereye shutdown` before reopening after this upgrade to load the renamed controller, then `build/evereye --replace-ui`. Shutdown ends an existing session. Replacing the UI alone preserves the running controller and its session.

## Validation

- Native qmake build: EverEye 0.4.0, no compiler warnings.
- Full regression suite: **68 passed, zero failed, zero skipped**, 81.3 seconds. Reports: `test-results/tests.txt` and `tests.xml`.
- Dashboard default-size fit, start/extend/end, failed-start switch reset, custom-duration synchronization, all-page navigation and narrow keyboard navigation passed.
- Small-window checks at 1100×540, 380×500 and 360×340 logical pixels passed, including actual wheel/Page Down scrolling and readable numeric fields.
- Additional checks with `QT_SCALE_FACTOR=2` passed: layout, editor geometry, keyboard navigation and duration synchronization (`test-results/evereye-hidpi-check.txt`).
- Overview, Sessions and Preferences renders visually inspected. Final reference-style preview: `test-results/evereye-overview.png`.
- Desktop entry validation, SVG parsing, shell/Python syntax and launcher/icon/service build references passed.
- `make INSTALL_ROOT=... install` staged the renamed executable, desktop file, icon and unit inside `test-results/evereye-staging`; the staged executable reports the correct version and renders its preview.

Live lid evidence remains from the original 0.1.0 user-run check. Current/legacy private-bus and forced kernel-supervisor evidence now passes in Ubuntu 22.04 and 24.04 containers; physical lid behavior still needs a 0.4.0 host check.

## Rounded window follow-up

Removed the outer 10-pixel padding and enabled a translucent top-level background so the rounded shell defines the app silhouette. Rendered PNG checks confirm all four outside corners have zero alpha and the center remains opaque. Existing default-fit, short-window scrolling and narrow keyboard-navigation regressions passed (three checks plus test setup/cleanup). Preview: `test-results/evereye-rounded.png`.
