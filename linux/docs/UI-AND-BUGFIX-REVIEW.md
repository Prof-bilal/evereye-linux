# UI and bug-fix review — 0.2.0

Reviewed 2026-11-06 against the local Linux source. This is an implementation review with regression evidence, not an independent audit or a claim that every defect has been found.

## Findings and fixes

| Finding | Fix and evidence |
|---|---|
| Long form clipped controls in tiled windows | Responsive cards inside a scroll area, with fixed header/footer. Real wheel and Page Down tests; minimum 360×340 window and 2× scaling checked. |
| Installed Qt stylesheet produced one-pixel-wide numeric editors | Explicit editor geometry and text styling. Tests check actual editor width, height and text, including focused fields; rendered PNGs inspected. |
| Synchronous controller polling could block the GUI for the D-Bus timeout | Requests moved to a serial QtConcurrent worker. A deliberately slow client test verifies GUI timers and scrolling still work and duplicate status reads are skipped. |
| Wheel scrolling over timer fields could change duration or fail to scroll | Both the spin box and its internal editor forward wheel events to the viewport; a regression checks scrolling and unchanged duration. |
| Local validation could disappear on the next status poll | Local errors persist until the user changes input or starts another operation. Regression verifies the next refresh preserves the message. |
| Settings did not reliably represent sessions started externally | Active session options sync from the controller and remain disabled during the session. Regression exercises an external start. |
| Failed power-saver release during an active AC transition was never retried | Retry while an owned hold remains, even after the desired state changes. Reproduced failing before the fix; now passes, and clears stale warnings after recovery. |
| End could wait on optional profile cleanup before dropping sleep inhibition | Release the inhibitor and stop the guardian first. Regression observes both already released when profile cleanup begins. |

## Validation

- Native qmake build succeeds without compiler warnings; executable reports 0.2.0.
- Full Qt suite: **63 passed, zero failed, zero skipped**, about 80 seconds; `test-results/tests.txt` and JUnit `tests.xml`.
- Additional minimum-window, wheel and numeric-editor checks pass; `test-results/geometry-check.txt`.
- The same layout/editor checks pass with `QT_SCALE_FACTOR=2`; `test-results/hidpi-check.txt`.
- Default and short-window screenshots visually inspected; `test-results/redesign-wide.png` and `build-tests/ui-short.png`.
- Python and shell runner syntax checked.

`--replace-ui` reopens this build by targeting only the registered, same-user UI through a pidfd. Controller sessions are preserved. Private-bus integration now includes a replacement test checking that the controller and inhibitor survive; this added transport test has not run here because private Unix sockets are blocked. Earlier user-run D-Bus and lid results apply to 0.1.0, not to a newly repeated 0.2.0 test.

No desktop configuration or installed package was changed. To load controller fixes, shut down the old controller (ending its current session) before reopening the new window. Existing 0.1.0 package files remain old artifacts; use the 0.2.0 executable or regenerated archives.
