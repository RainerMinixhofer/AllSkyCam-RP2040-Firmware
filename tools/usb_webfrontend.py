#!/usr/bin/env python3
"""Local web frontend for RP2040 USB CDC status + command control."""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler
from http.server import ThreadingHTTPServer
from typing import Any
from urllib.parse import urlparse

import serial
from serial import SerialException
from serial import SerialTimeoutException
from serial.tools import list_ports


HTML_PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>RP2040 Periphery Control</title>
  <style>
    body { font-family: sans-serif; margin: 1rem; background:#111; color:#eee; }
    h1, h2 { margin: 0.4rem 0; }
    .grid { display: grid; grid-template-columns: repeat(auto-fill,minmax(260px,1fr)); gap: 1rem; }
    .card { border:1px solid #444; border-radius:8px; padding:0.8rem; background:#1c1c1c; }
    button { margin:0.2rem; padding:0.35rem 0.7rem; }
    input[type=number], input[type=text] { width: 100%; padding:0.35rem; margin:0.2rem 0; }
    pre { background:#000; color:#0f0; padding:0.6rem; border-radius:6px; max-height:300px; overflow:auto; }
    .ok { color:#7CFC00; }
    .bad { color:#ff6b6b; }
  </style>
</head>
<body>
  <h1>RP2040 Periphery Status + Commands</h1>
  <p id="conn">Connecting...</p>

  <div class="grid">
    <div class="card">
      <h2>Power / Driver</h2>
      <button onclick="sendCmd('SET CAM_PWR 1')">Camera ON</button>
      <button onclick="sendCmd('SET CAM_PWR 0')">Camera OFF</button><br/>
      <button onclick="sendCmd('SET DRV_EN 1')">Driver EN</button>
      <button onclick="sendCmd('SET DRV_EN 0')">Driver DIS</button><br/>
      <label>Dew PWM %</label>
      <input type="number" id="dew" min="0" max="100" value="0"/>
      <button onclick="setDew()">Set Dew</button>
    </div>

    <div class="card">
      <h2>Geiger / MCP / TMC</h2>
      <button onclick="sendCmd('GET GEIGER_STATS')">Get Geiger Stats</button>
      <button onclick="sendCmd('MCP TEST')">MCP TEST</button>
      <button onclick="sendCmd('I2C SCAN')">I2C SCAN</button>
      <button onclick="sendCmd('TMC CFG')">TMC CFG</button><br/>
      <label>TMC TEST axis (0..3)</label>
      <input type="number" id="tmcAxis" min="0" max="3" value="0"/>
      <button onclick="tmcTest()">Run TMC TEST</button><br/>
      <label>TMC READ axis reg (e.g. "0 0x6F")</label>
      <input type="text" id="tmcRead" value="0 0x6F"/>
      <button onclick="tmcRead()">Run TMC READ</button>
    </div>

    <div class="card">
      <h2>Available Commands</h2>
      <pre id="commands">loading...</pre>
    </div>
  </div>

  <h2>Live Status</h2>
  <pre id="status">{}</pre>

  <h2>Last Command Result</h2>
  <pre id="result">-</pre>

<script>
async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
  return await r.json();
}

async function postJson(url, data) {
  const r = await fetch(url, {
    method: "POST",
    headers: { "Content-Type":"application/json" },
    body: JSON.stringify(data)
  });
  const payload = await r.json();
  if (!r.ok) throw new Error(JSON.stringify(payload));
  return payload;
}

function setDew() {
  const v = Number(document.getElementById("dew").value || 0);
  sendCmd(`SET DEW_PWM ${Math.max(0, Math.min(100, v|0))}`);
}

function tmcTest() {
  const axis = Number(document.getElementById("tmcAxis").value || 0);
  sendCmd(`TMC TEST ${Math.max(0, Math.min(3, axis|0))}`);
}

function tmcRead() {
  const txt = document.getElementById("tmcRead").value.trim();
  sendCmd(`TMC READ ${txt}`);
}

async function sendCmd(command) {
  try {
    const payload = await postJson("/api/command", { command });
    document.getElementById("result").textContent = JSON.stringify(payload, null, 2);
  } catch (e) {
    document.getElementById("result").textContent = `ERROR: ${e}`;
  }
}

async function refresh() {
  try {
    const s = await getJson("/api/status");
    document.getElementById("conn").innerHTML =
      s.connected ? `<span class="ok">Connected</span>` : `<span class="bad">Disconnected</span>`;
    document.getElementById("status").textContent = JSON.stringify(s, null, 2);
  } catch (e) {
    document.getElementById("conn").innerHTML = `<span class="bad">API error: ${e}</span>`;
  }
}

async function loadCommands() {
  try {
    const c = await getJson("/api/commands");
    document.getElementById("commands").textContent = c.commands.join("\\n");
  } catch (e) {
    document.getElementById("commands").textContent = `ERROR: ${e}`;
  }
}

loadCommands();
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
"""


AVAILABLE_COMMANDS = [
    "GET STATUS",
    "GET GEIGER_STATS",
    "MCP TEST",
    "I2C SCAN",
    "SET CAM_PWR <0|1>",
    "SET DEW_PWM <0..100>",
    "SET DRV_EN <0|1>",
    "SET COINC_WINDOW_US <1..1000000>",
    "SET GEIGER_DEBOUNCE_US <1..1000000>",
    "MOVE <axis:0..3> <steps> <dir:0|1>",
    "TMC CFG",
    "TMC TEST <axis:0..3>",
    "TMC READ <axis:0..3> <reg:0..127|0x00..0x7F>",
]


def auto_detect_port() -> str:
    for p in list_ports.comports():
        text = f"{p.description} {p.manufacturer} {p.hwid}".lower()
        if "raspberry" in text or "pico" in text or "2e8a" in text:
            return p.device
    raise RuntimeError("Could not auto-detect RP2040 CDC port. Pass --port explicitly.")


@dataclass
class CdcClient:
    ser: serial.Serial
    lock: threading.Lock

    def write_line(self, line: str) -> None:
        self.ser.write((line + "\n").encode("utf-8"))
        self.ser.flush()

    def read_json(self, timeout_s: float, expected_type: str | None = None) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            raw = self.ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            start = text.find("{")
            end = text.rfind("}")
            if start < 0 or end <= start:
                continue
            try:
                payload = json.loads(text[start : end + 1])
            except json.JSONDecodeError:
                continue
            if expected_type is None or payload.get("type") == expected_type:
                return payload
        raise TimeoutError(f"Timed out waiting for JSON response type={expected_type!r}")

    def run_command(self, command: str, timeout_s: float) -> dict[str, Any]:
        responses: list[dict[str, Any]] = []
        with self.lock:
            self.write_line(command)
            deadline = time.monotonic() + timeout_s
            ack: dict[str, Any] | None = None
            while time.monotonic() < deadline:
                try:
                    payload = self.read_json(max(0.1, deadline - time.monotonic()))
                except TimeoutError:
                    continue
                p_type = payload.get("type")
                if p_type == "ack":
                    ack = payload
                    break
                responses.append(payload)
            if ack is None:
                raise TimeoutError("No ACK received for command")
            if not ack.get("ok", False):
                raise RuntimeError(f"Command failed: {ack}")
        return {"command": command, "ack": ack, "responses": responses}


class DeviceBridge:
    def __init__(self, client: CdcClient, poll_interval: float, timeout_s: float) -> None:
        self.client = client
        self.poll_interval = poll_interval
        self.timeout_s = timeout_s
        self._latest_status: dict[str, Any] = {}
        self._last_error: str | None = None
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._poll_loop, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.5)

    def snapshot(self) -> dict[str, Any]:
        return {
            "connected": self._last_error is None,
            "last_error": self._last_error,
            "status": self._latest_status,
            "commands": AVAILABLE_COMMANDS,
        }

    def run_command(self, command: str) -> dict[str, Any]:
        result = self.client.run_command(command, timeout_s=self.timeout_s)
        for payload in result["responses"]:
            if payload.get("type") == "status":
                self._latest_status = payload
        self._last_error = None
        return result

    def _poll_loop(self) -> None:
        while not self._stop.is_set():
            try:
                result = self.client.run_command("GET STATUS", timeout_s=self.timeout_s)
                for payload in result["responses"]:
                    if payload.get("type") == "status":
                        self._latest_status = payload
                        break
                self._last_error = None
            except Exception as exc:  # explicit status propagation for frontend
                self._last_error = str(exc)
            self._stop.wait(self.poll_interval)


def make_handler(bridge: DeviceBridge) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def _json(self, payload: dict[str, Any], code: int = 200) -> None:
            body = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802
            path = urlparse(self.path).path
            if path == "/":
                body = HTML_PAGE.encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if path == "/api/status":
                self._json(bridge.snapshot())
                return
            if path == "/api/commands":
                self._json({"commands": AVAILABLE_COMMANDS})
                return
            self._json({"error": "not_found"}, code=404)

        def do_POST(self) -> None:  # noqa: N802
            path = urlparse(self.path).path
            if path != "/api/command":
                self._json({"error": "not_found"}, code=404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self._json({"error": "invalid_content_length"}, code=400)
                return
            payload_raw = self.rfile.read(length)
            try:
                payload = json.loads(payload_raw.decode("utf-8"))
            except json.JSONDecodeError:
                self._json({"error": "invalid_json"}, code=400)
                return
            command = str(payload.get("command", "")).strip()
            if not command:
                self._json({"error": "missing_command"}, code=400)
                return
            try:
                result = bridge.run_command(command)
            except Exception as exc:
                self._json({"error": str(exc)}, code=500)
                return
            self._json(result)

        def log_message(self, fmt: str, *args: Any) -> None:
            return

    return Handler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run local RP2040 USB CDC web frontend.")
    parser.add_argument("--port", help="Serial port (e.g. /dev/ttyACM0). Auto-detected if omitted.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="CDC command timeout in seconds.")
    parser.add_argument("--poll-interval", type=float, default=1.0, help="Status poll interval seconds.")
    parser.add_argument("--host", default="127.0.0.1", help="HTTP bind host (default: 127.0.0.1)")
    parser.add_argument("--http-port", type=int, default=8765, help="HTTP bind port (default: 8765)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.poll_interval <= 0:
        print("[FAIL] --poll-interval must be > 0", file=sys.stderr)
        return 1
    port = args.port or auto_detect_port()
    print(f"[INFO] Using serial port: {port}", flush=True)
    try:
        with serial.Serial(
            port=port,
            baudrate=args.baud,
            timeout=0.2,
            write_timeout=2.0,
            exclusive=True,
        ) as ser:
            ser.dtr = True
            ser.rts = False
            time.sleep(0.5)
            ser.reset_input_buffer()

            client = CdcClient(ser=ser, lock=threading.Lock())
            bridge = DeviceBridge(client=client, poll_interval=args.poll_interval, timeout_s=args.timeout)
            bridge.start()
            try:
                handler = make_handler(bridge)
                server = ThreadingHTTPServer((args.host, args.http_port), handler)
                print(f"[INFO] Web frontend: http://{args.host}:{args.http_port}", flush=True)
                server.serve_forever()
            finally:
                bridge.stop()
    except (SerialException, SerialTimeoutException) as exc:
        print(f"[FAIL] Serial error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n[INFO] Stopped by user.", flush=True)
        return 0
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
