#!/usr/bin/env python3
"""Exercise the EverEye gateway: TLS refusal, auth, job lifecycle and SSE.

Run this under a private bus so it never touches the live user session:

    dbus-run-session -- python3 scripts/test-gateway.py
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(os.environ.get("EVEREYE_BINARY", root / "build/evereye"))
if not binary.is_file():
    sys.exit("Build first: ./scripts/build.sh")

token = "0" * 63 + "1"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for(fn, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if fn():
                return True
        except Exception:
            pass
        time.sleep(0.15)
    return False


def request(url, token_value=None, method="GET", body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if token_value:
        req.add_header("Authorization", "Bearer " + token_value)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=8) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def check(condition, label):
    if not condition:
        sys.exit("FAIL: " + label)
    print("ok -", label)


def tls_refusal(tmp):
    port = free_port()
    config = Path(tmp) / "refuse" / "evereye"
    config.mkdir(parents=True)
    (config / "gateway.json").write_text(json.dumps({
        "token": token, "bind": "0.0.0.0", "port": port,
        "enabled": True, "selfSigned": False, "allowArbitraryCommands": False,
    }))
    env = dict(os.environ, XDG_CONFIG_HOME=str(Path(tmp) / "refuse"))
    result = subprocess.run([str(binary), "gateway", "--bind", "0.0.0.0", "--port", str(port)],
                            capture_output=True, text=True, timeout=20, env=env)
    check(result.returncode != 0, "non-loopback plaintext bind is refused")
    check("TLS" in (result.stdout + result.stderr), "refusal explains TLS requirement")

    # A safe config must not let a CLI override slip past the TLS gate.
    (config / "gateway.json").write_text(json.dumps({
        "token": token, "bind": "127.0.0.1", "port": port,
        "enabled": True, "selfSigned": False, "allowArbitraryCommands": False,
    }))
    override = subprocess.run([str(binary), "gateway", "--bind", "0.0.0.0", "--port", str(port)],
                              capture_output=True, text=True, timeout=20, env=env)
    check(override.returncode != 0, "CLI bind override is re-validated against TLS")


def live(tmp):
    state = Path(tmp) / "state"
    config = Path(tmp) / "config"
    data = Path(tmp) / "data"
    (data / "evereye" / "web").mkdir(parents=True)
    (data / "evereye" / "web" / "index.html").write_text("<!doctype html><title>evereye-pwa-marker</title>")
    (config / "evereye").mkdir(parents=True)
    (config / "evereye" / "tasks.json").write_text(json.dumps([
        {"name": "hello", "command": "sh", "args": ["-c", "printf hello-from-job; sleep 2"]},
        {"name": "echo", "command": "cat"},
    ]))
    port = free_port()
    (config / "evereye" / "gateway.json").write_text(json.dumps({
        "token": token, "bind": "127.0.0.1", "port": port,
        "enabled": True, "selfSigned": False, "allowArbitraryCommands": False,
    }))
    env = dict(os.environ, XDG_CONFIG_HOME=str(config), XDG_STATE_HOME=str(state), XDG_DATA_HOME=str(data))
    daemon = subprocess.Popen([str(binary), "--daemon"], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    gateway = subprocess.Popen([str(binary), "gateway", "--bind", "127.0.0.1", "--port", str(port)], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        base = f"http://127.0.0.1:{port}"
        check(wait_for(lambda: request(base + "/api/status")[0] == 401), "unauthenticated request is 401")
        served = request(base + "/")
        check(served[0] == 200 and "evereye-pwa-marker" in served[1], "gateway serves the built PWA without auth")
        check(request(base + "/api/status", "wrong", "GET")[0] == 401, "wrong token is 401")
        status, body = request(base + "/api/status", token, "GET")
        check(status == 200 and json.loads(body).get("ok") is True, "authenticated status is 200")
        started = json.loads(request(base + "/api/jobs", token, "POST", {"preset": "hello"})[1])
        check(started.get("ok") is True, "start preset job")
        job_id = started["id"]
        listed = json.loads(request(base + "/api/jobs", token)[1])
        check(any(j["id"] == job_id for j in listed["jobs"]), "job appears in list")
        stream_req = urllib.request.Request(base + f"/api/jobs/{job_id}/stream")
        stream_req.add_header("Authorization", "Bearer " + token)
        with urllib.request.urlopen(stream_req, timeout=10) as response:
            payload = response.read().decode()
        check("hello-from-job" in payload, "SSE stream carries live output")
        arbitrary = request(base + "/api/jobs", token, "POST", {"command": "sh", "args": ["-c", "true"]})
        check(arbitrary[0] == 403, "arbitrary command is refused by default")
        check(json.loads(request(base + "/api/jobs", token, "POST", {"preset": "hello"})[1]).get("ok") is True,
              "second preset job starts")

        echoed = json.loads(request(base + "/api/jobs", token, "POST", {"preset": "echo"})[1])
        check(echoed.get("ok") is True, "interactive job starts")
        echo_id = echoed["id"]
        sent = json.loads(request(base + f"/api/jobs/{echo_id}/input", token, "POST", {"data": "ping\n"})[1])
        check(sent.get("ok") is True, "input is accepted")
        stream_req = urllib.request.Request(base + f"/api/jobs/{echo_id}/stream")
        stream_req.add_header("Authorization", "Bearer " + token)
        echoed_back = False
        with urllib.request.urlopen(stream_req, timeout=10) as response:
            deadline = time.time() + 8
            while time.time() < deadline:
                line = response.readline().decode()
                if "ping" in line:
                    echoed_back = True
                    break
        check(echoed_back, "PTY echoes typed input")
        request(base + f"/api/jobs/{echo_id}/stop", token, "POST")
    finally:
        for process in (gateway, daemon):
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()


def main():
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        sys.exit("Run under: dbus-run-session -- python3 scripts/test-gateway.py")
    with tempfile.TemporaryDirectory() as tmp:
        tls_refusal(tmp)
        live(tmp)
    print("gateway integration passed")


if __name__ == "__main__":
    main()
