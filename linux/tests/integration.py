#!/usr/bin/env python3
"""Real D-Bus/Unix-FD transport tests on two names of ONE private bus.

Run through scripts/test-dbus.sh. Never connects to the live system bus.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("binary")
parser.add_argument("fixtures")
parser.add_argument("--profile-api", choices=["current", "legacy"], default="current")
options = parser.parse_args()
binary = Path(options.binary).resolve()
fixtures = Path(options.fixtures).resolve()
assert os.environ.get("DBUS_SESSION_BUS_ADDRESS"), "private session bus required"
assert 'EVEREYE_PRIVATE_BUS_PARENT' in os.environ and os.environ['EVEREYE_PRIVATE_BUS_PARENT'] != os.environ['DBUS_SESSION_BUS_ADDRESS'], 'Use scripts/private-bus-run.py; refusing a possibly live user bus' 
env = os.environ.copy()
env["DBUS_SYSTEM_BUS_ADDRESS"] = env["DBUS_SESSION_BUS_ADDRESS"]
LOGIN = ("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager")
BATTERY = ("org.freedesktop.UPower", "/org/freedesktop/UPower/devices/battery_BAT0", "org.freedesktop.UPower.Device")
UPOWER = ("org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.UPower")
PROFILES = ("org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles", "org.freedesktop.UPower.PowerProfiles")
if options.profile_api == "legacy":
    PROFILES = ("net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles")


def bus_call(target, member, *args):
    service, path, interface = target
    return subprocess.check_output(["gdbus", "call", "--session", "--dest", service,
        "--object-path", path, "--method", interface + "." + member, *map(str, args)], env=env, text=True).strip()


def prop(target, name):
    service, path, interface = target
    text = subprocess.check_output(["gdbus", "call", "--session", "--dest", service,
        "--object-path", path, "--method", "org.freedesktop.DBus.Properties.Get", interface, name], env=env, text=True)
    import re
    match = re.search(r"<(?:uint32 )?(\d+)>", text)
    assert match, text
    return int(match[1])


def command(*args, ok=True):
    result = subprocess.run([str(binary), *args], env=env, capture_output=True, text=True, timeout=20)
    data = json.loads(result.stdout)
    assert data.get("ok") is ok, (args, result.returncode, data, result.stderr)
    assert (result.returncode == 0) is ok, (args, result.returncode, data)
    return data


def until(check, seconds=5, interval=0.1):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if check():
            return
        time.sleep(interval)
    raise AssertionError("Timed out waiting for condition")


def controller_ready():
    return subprocess.run(["gdbus", "introspect", "--session", "--dest", "io.github.ProfBilal.Insomnia1",
        "--object-path", "/io/github/ProfBilal/Insomnia"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


with tempfile.TemporaryDirectory(prefix="insomnia-integration-") as root:
    env["XDG_STATE_HOME"] = root
    fake = subprocess.Popen([str(fixtures)], env=env)
    daemon = None
    windows = []
    try:
        until(lambda: subprocess.run(["gdbus", "introspect", "--session", "--dest", LOGIN[0], "--object-path", LOGIN[1]], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0, seconds=15)
        # Captured CLI output must reach EOF even when it launches a detached daemon.
        assert not command("status")["active"]
        command("shutdown")
        until(lambda: not controller_ready(), seconds=15)
        daemon = subprocess.Popen([str(binary), "--daemon"], env=env)
        until(controller_ready, seconds=15)
        capabilities = command("doctor")
        assert capabilities["logind"] and capabilities["supervisor"]
        if env.get('EVEREYE_SUPERVISOR') == 'kernel-timer':
            assert capabilities['supervisorBackend'] == 'kernel-timer'
        assert capabilities["powerProfilesService"] == PROFILES[0]
        print("CAPABILITIES",json.dumps(capabilities,sort_keys=True),flush=True)
        assert command("start", "60s")["active"]
        until(lambda: prop(LOGIN, "ActiveLocks") == 1)
        # Replacing an already-running UI must preserve the controller's session.
        ui_env = env | {"QT_QPA_PLATFORM": "offscreen", "QT_QPA_PLATFORMTHEME": "", "QT_STYLE_OVERRIDE": "Fusion"}
        ui_target = ("io.github.ProfBilal.Insomnia1.UI", "/Window", "io.github.ProfBilal.Insomnia.Window")
        first = subprocess.Popen([str(binary)], env=ui_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        windows.append(first)
        def ui_ready():
            return subprocess.run(["gdbus", "introspect", "--session", "--dest", ui_target[0], "--object-path", ui_target[1]], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        until(ui_ready, seconds=15)
        replacement = subprocess.Popen([str(binary), "--replace-ui"], env=ui_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        windows.append(replacement)
        first.wait(timeout=15)
        until(ui_ready, seconds=15)
        assert replacement.poll() is None
        assert daemon.poll() is None and command("status")["active"]
        assert prop(LOGIN, "ActiveLocks") == 1
        bus_call(ui_target, "QuitWindow")
        replacement.wait(timeout=5)
        command("start", "60s", ok=False)
        # Allow slower package/container startup while still requiring the
        # extension to materially increase the roughly 60-second session.
        assert command("extend", "60s")["remainingSeconds"] >= 100
        command("end")
        until(lambda: prop(LOGIN, "ActiveLocks") == 0)
        bus_call(LOGIN, "Deny", "true")
        assert not command("start", "60s", ok=False)["active"]
        bus_call(LOGIN, "Deny", "false")
        bus_call(UPOWER, "SetOnBattery", "true")
        bus_call(BATTERY, "SetPercentage", "30.0")
        assert command("start", "60s")["powerSaverHeld"]
        until(lambda: prop(PROFILES, "Holds") == 1)
        bus_call(PROFILES, "ManualOverride")
        until(lambda: not command("status")["powerSaverHeld"])
        command("end")
        bus_call(BATTERY, "SetPercentage", "9.0")
        command("start", "60s", ok=False)
        bus_call(BATTERY, "SetPercentage", "80.0")
        bus_call(UPOWER, "SetOnBattery", "false")
        command("start", "60s")
        import re
        pid_reply=bus_call(("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus"),"GetConnectionUnixProcessID","io.github.ProfBilal.Insomnia1")
        match=re.search(r"uint32 (\d+)",pid_reply)
        assert match, pid_reply
        # AppImage/Flatpak launchers can be different processes from the service.
        os.kill(int(match[1]),signal.SIGKILL)
        daemon.wait(timeout=5)
        until(lambda: prop(LOGIN, "ActiveLocks") == 0)
        daemon = subprocess.Popen([str(binary), "--daemon"], env=env)
        until(controller_ready, seconds=15)
        assert not command("status")["active"], "interrupted session silently resumed"
        command("start", "60s")
        # Poll gently across the whole session: spawning the CLI 10x/second for
        # a minute can starve the controller's own timer on a loaded CI runner,
        # which the deadline watchdog would then enforce with SIGKILL.
        until(lambda: not command("status")["active"], seconds=65, interval=0.5)
        until(lambda: prop(LOGIN, "ActiveLocks") == 0, interval=0.25)
        assert daemon.poll() is None, f"normal expiry should not kill the controller (rc={daemon.returncode})"
        command("shutdown")
        daemon.wait(timeout=5)
        print(f"PASS ({options.profile_api} profile API): real private-bus start/extend/end, UI replacement, denied inhibition, profile holds/manual override, battery cutoff, crash FD release, reconciliation, and 60-second expiry")
    finally:
        for window in windows:
            if window.poll() is None:
                window.terminate()
                window.wait(timeout=5)
        if daemon and daemon.poll() is None:
            daemon.terminate()
            try:
                daemon.wait(timeout=5)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.wait()
        fake.terminate()
        fake.wait(timeout=5)
