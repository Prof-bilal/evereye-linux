#!/usr/bin/env python3
"""Exercise the EverEye gateway: TLS refusal, auth, job lifecycle and SSE.

Run this under a private bus so it never touches the live user session:

    dbus-run-session -- python3 scripts/test-gateway.py
"""
import base64
import json
import os
import select
import socket
import struct
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


def ws_connect(port, path, token):
    sock = socket.create_connection(("127.0.0.1", port), timeout=8)
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
        f"Sec-WebSocket-Protocol: evereye, {token}\r\n\r\n"
    )
    sock.sendall(request.encode())
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    head, _, rest = data.partition(b"\r\n\r\n")
    return sock, head, rest


def ws_send(sock, opcode, payload):
    mask = os.urandom(4)
    header = bytes([0x80 | opcode])
    length = len(payload)
    if length < 126:
        header += bytes([0x80 | length])
    elif length <= 0xFFFF:
        header += bytes([0x80 | 126]) + struct.pack(">H", length)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", length)
    sock.sendall(header + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload)))


def ws_read(sock, buffer):
    def ensure(size):
        nonlocal buffer
        while len(buffer) < size:
            chunk = sock.recv(4096)
            if not chunk:
                raise EOFError
            buffer += chunk

    ensure(2)
    first, second = buffer[0], buffer[1]
    length = second & 0x7F
    offset = 2
    if length == 126:
        ensure(4)
        length = struct.unpack(">H", buffer[2:4])[0]
        offset = 4
    elif length == 127:
        ensure(10)
        length = struct.unpack(">Q", buffer[2:10])[0]
        offset = 10
    ensure(offset + length)
    payload = buffer[offset:offset + length]
    buffer = buffer[offset + length:]
    return first & 0x0F, payload, buffer


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

        attach = subprocess.Popen([str(binary), "attach", echo_id], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        attach.stdin.write(b"attach-cli\n")
        attach.stdin.flush()
        os.set_blocking(attach.stdout.fileno(), False)
        imported = False
        deadline = time.time() + 6
        while time.time() < deadline:
            readable, _, _ = select.select([attach.stdout], [], [], 0.2)
            if readable and b"attach-cli" in os.read(attach.stdout.fileno(), 4096):
                imported = True
                break
        attach.terminate()
        attach.wait(timeout=5)
        check(imported, "evereye attach bridges this terminal to the job")

        sock, head, rest = ws_connect(port, f"/api/jobs/{echo_id}/terminal", token)
        check(head.startswith(b"HTTP/1.1 101"), "WebSocket terminal upgrade succeeds")
        sock.settimeout(6)
        ws_send(sock, 0x2, b"ws-pty\n")
        streamed = False
        for _ in range(40):
            opcode, payload, rest = ws_read(sock, rest)
            if b"ws-pty" in payload:
                streamed = True
                break
            if opcode == 0x8:
                break
        check(streamed, "WebSocket streams PTY output")
        ws_send(sock, 0x1, json.dumps({"type": "resize", "cols": 100, "rows": 30}).encode())
        ws_send(sock, 0x8, b"\x03\xe8")
        sock.close()

        bad, bad_head, _ = ws_connect(port, f"/api/jobs/{echo_id}/terminal", "wrong-token")
        check(b" 401 " in bad_head.split(b"\r\n")[0], "WebSocket rejects a wrong token")
        bad.close()

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
