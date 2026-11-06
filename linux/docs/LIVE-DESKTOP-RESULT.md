# Live desktop validation evidence

Date: 2026-11-06, Asia/Karachi. Environment: user's Omarchy/Hyprland desktop; the build environment reports Omarchy 4.0.6 on x86_64, Qt 6.12.0 and systemd 262.1.

The user supplied a screenshot of the actual native window connected to its controller, reading battery 99% and AC connected. The image exposed options clipping in a tiled window. The implementation was changed to a centered, vertically scrollable form; automated captures and geometry assertions cover large/short tiled layouts. The user has not separately confirmed the updated layout screenshot.

After instructions to close the lid for about 20 seconds, the user ran:

```bash
/home/abullah/Projects/OpenEye/linux/scripts/test-lid.sh
```

The posted output shows a one-minute session starting, progress throughout the 40-second measurement, then:

```text
Largest execution pause: 1.00 seconds
No long execution pause observed
```

The script measures Linux CLOCK_BOOTTIME differences between one-second executions, so a system suspend during the measurement would normally appear as a much longer execution gap. This is evidence that the workload kept running during the instructed lid-close test. Lid movement itself was performed by the user and was not independently recorded. This record is a short supervised result, not a thermal safety guarantee or evidence for other desktops/hardware.

The result was also saved by the user's script to `linux/test-results/lid-check.json` in the build workspace. It preserves sample count, duration, largest gap, and the requirement for user confirmation of physical lid closure. It is not included as an assumption that every future run closes the lid.

The agent subsequently read that result file: 40 samples, largest gap 1.001 seconds. A read-only check of the actual app journal at `~/.local/state/insomnia-linux/session.json` showed `active: false`, `reason: "Timer expired"`, `lid: true`, and `powerSaver: false`. This independently records the normal expiry state after the one-minute live session. No personal boot identifier is reproduced here.
