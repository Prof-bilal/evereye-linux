# User-terminal D-Bus integration evidence

Received 2026-11-06 (Asia/Karachi) in the active task conversation. The user ran:

```bash
/home/abullah/Projects/OpenEye/linux/scripts/test-dbus.sh
```

The posted terminal output shows compilation of the platform, service and window modules, successful linking, version 0.1.0, and completion with:

```text
PASS: real private-bus start/extend/end, denied inhibition, profile holds/manual override, battery cutoff, crash FD release, reconciliation, and 60-second expiry
```

This is user-supplied runtime evidence from the normal terminal. The same suite could not run inside the agent sandbox because local Unix socket binding was denied. The script runs production transport against isolated fake system services; this result does not prove live hardware or desktop lid behavior.
