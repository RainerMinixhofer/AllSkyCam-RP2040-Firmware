#!/usr/bin/env python3
"""USB CDC smoke test for the RP2040 controller firmware."""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from typing import Any

import serial
from serial import SerialException
from serial import SerialTimeoutException
from serial.tools import list_ports


@dataclass
class CdcClient:
    ser: serial.Serial
    verbose: bool = False

    def _log(self, direction: str, line: str) -> None:
        if self.verbose:
            print(f"[{direction}] {line}", flush=True)

    def write_line(self, line: str) -> None:
        last_exc: Exception | None = None
        for _ in range(3):
            try:
                self._log("TX", line)
                self.ser.write((line + "\n").encode("utf-8"))
                self.ser.flush()
                return
            except (SerialTimeoutException, SerialException) as exc:
                last_exc = exc
                time.sleep(0.25)
        raise RuntimeError(
            "Serial write timed out after retries. The device may be resetting "
            "or another app is intermittently touching the port."
        ) from last_exc

    def read_line(self, timeout_s: float) -> str:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                raw = self.ser.readline()
            except SerialException as exc:
                raise RuntimeError(
                    f"Serial read failed: {exc}. Close any serial monitor and retry."
                ) from exc
            if raw:
                line = raw.decode("utf-8", errors="replace").strip()
                self._log("RX", line)
                return line
        raise TimeoutError("Timed out waiting for serial line")

    def read_json(self, timeout_s: float, expected_type: str | None = None) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                line = self.read_line(max(0.05, deadline - time.monotonic()))
            except TimeoutError:
                continue
            if not line:
                continue
            start = line.find("{")
            end = line.rfind("}")
            if start < 0 or end <= start:
                continue
            try:
                payload = json.loads(line[start : end + 1])
            except json.JSONDecodeError:
                continue
            if expected_type is None or payload.get("type") == expected_type:
                return payload
        raise TimeoutError(f"Timed out waiting for JSON response type={expected_type!r}")


def auto_detect_port() -> str:
    for p in list_ports.comports():
        text = f"{p.description} {p.manufacturer} {p.hwid}".lower()
        if "raspberry" in text or "pico" in text or "2e8a" in text:
            return p.device
    raise RuntimeError("Could not auto-detect RP2040 CDC port. Pass --port explicitly.")


def expect_ok_ack(client: CdcClient, timeout_s: float) -> None:
    ack = client.read_json(timeout_s=timeout_s, expected_type="ack")
    if not ack.get("ok", False):
        raise AssertionError(f"ACK reported failure: {ack}")


def expect_status_shape(status: dict[str, Any]) -> None:
    required_top = {"type", "cam", "dew", "drv_en", "geiger", "park", "mcp", "thermal", "tmc", "tmc_error"}
    missing = required_top - set(status.keys())
    if missing:
        raise AssertionError(f"Status missing keys: {sorted(missing)}")
    if status["type"] != "status":
        raise AssertionError(f"Expected status type, got {status['type']!r}")
    if not isinstance(status["park"], list) or len(status["park"]) != 3:
        raise AssertionError("Status park list must have 3 entries")
    if not isinstance(status["mcp"], dict):
        raise AssertionError("Status mcp object missing")
    if not isinstance(status["tmc"], list) or len(status["tmc"]) != 4:
        raise AssertionError("Status tmc list must have 4 entries")


def run_test_once(client: CdcClient, timeout_s: float, startup_timeout_s: float) -> None:
    # Allow CDC port to stabilize after open/reset and wait for passive status first.
    print(f"[INFO] Waiting for controller status (up to {startup_timeout_s:.0f}s)...", flush=True)
    settle_deadline = time.monotonic() + startup_timeout_s
    next_progress = time.monotonic() + 1.0
    saw_status = False
    while time.monotonic() < settle_deadline:
        try:
            payload = client.read_json(0.6, expected_type="status")
            if payload.get("type") == "status":
                saw_status = True
                break
        except TimeoutError:
            now = time.monotonic()
            if now >= next_progress:
                remaining = max(0.0, settle_deadline - now)
                print(f"[INFO] ...still waiting ({remaining:.0f}s left)", flush=True)
                next_progress = now + 1.0
            continue
    if not saw_status:
        raise TimeoutError(
            "No status JSON received after opening port. "
            "Ensure no other app (PlatformIO monitor, minicom, screen) has the port open."
        )
    print("[INFO] Controller is responding.", flush=True)

    client.write_line("GET STATUS")
    status = client.read_json(timeout_s=timeout_s, expected_type="status")
    expect_status_shape(status)
    expect_ok_ack(client, timeout_s=timeout_s)

    initial_cam = int(status["cam"])
    initial_dew = int(status["dew"])
    initial_drv = int(status["drv_en"])
    new_cam = 0 if initial_cam else 1
    new_drv = 0 if initial_drv else 1
    new_dew = 37 if initial_dew == 0 else 0

    try:
        client.write_line(f"SET CAM_PWR {new_cam}")
        expect_ok_ack(client, timeout_s=timeout_s)

        client.write_line(f"SET DEW_PWM {new_dew}")
        expect_ok_ack(client, timeout_s=timeout_s)

        client.write_line(f"SET DRV_EN {new_drv}")
        expect_ok_ack(client, timeout_s=timeout_s)

        client.write_line("SET COINC_WINDOW_US 75")
        expect_ok_ack(client, timeout_s=timeout_s)

        client.write_line("SET GEIGER_DEBOUNCE_US 150")
        expect_ok_ack(client, timeout_s=timeout_s)

        client.write_line("MOVE 0 10 1")
        expect_ok_ack(client, timeout_s=timeout_s)

        client.write_line("TMC READ 0 0x6F")
        tmc_read = client.read_json(timeout_s=timeout_s, expected_type="tmc_read")
        if "ok" not in tmc_read or "value" not in tmc_read:
            raise AssertionError(f"Malformed tmc_read response: {tmc_read}")

        client.write_line("MCP TEST")
        mcp_test = client.read_json(timeout_s=timeout_s, expected_type="mcp_test")
        if "ok" not in mcp_test or "park" not in mcp_test:
            raise AssertionError(f"Malformed mcp_test response: {mcp_test}")
        if not isinstance(mcp_test["park"], list) or len(mcp_test["park"]) != 3:
            raise AssertionError(f"Malformed MCP park switch list: {mcp_test}")
http://127.0.0.1:8765
        client.write_line("GET STATUS")
        changed_status = client.read_json(timeout_s=timeout_s, expected_type="status")
        expect_status_shape(changed_status)
        expect_ok_ack(client, timeout_s=timeout_s)

        if int(changed_status["cam"]) != new_cam:
            raise AssertionError(f"Expected cam={new_cam}, got {changed_status['cam']}")
        if int(changed_status["drv_en"]) != new_drv:
            raise AssertionError(f"Expected drv_en={new_drv}, got {changed_status['drv_en']}")
        if int(changed_status["dew"]) != new_dew:
            raise AssertionError(f"Expected dew={new_dew}, got {changed_status['dew']}")
    finally:
        # Restore original values.
        client.write_line(f"SET CAM_PWR {initial_cam}")
        expect_ok_ack(client, timeout_s=timeout_s)
        client.write_line(f"SET DEW_PWM {initial_dew}")
        expect_ok_ack(client, timeout_s=timeout_s)
        client.write_line(f"SET DRV_EN {initial_drv}")
        expect_ok_ack(client, timeout_s=timeout_s)


def run_soak_test(
    client: CdcClient,
    timeout_s: float,
    startup_timeout_s: float,
    loops: int,
    duration_s: float,
) -> None:
    start = time.monotonic()
    iteration = 0
    while True:
        elapsed = time.monotonic() - start
        if loops > 0 and iteration >= loops:
            break
        if duration_s > 0 and elapsed >= duration_s:
            break
        iteration += 1
        print(f"[INFO] Iteration {iteration}...", flush=True)
        per_iter_startup = startup_timeout_s if iteration == 1 else min(2.0, startup_timeout_s)
        run_test_once(client, timeout_s=timeout_s, startup_timeout_s=per_iter_startup)
    print(f"[INFO] Completed {iteration} iteration(s).", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Smoke-test RP2040 USB CDC JSON protocol.")
    parser.add_argument("--port", help="Serial port (e.g. /dev/ttyACM0). Auto-detected if omitted.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="Read timeout in seconds (default: 2.0)")
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=8.0,
        help="Time to wait for first status JSON after opening the port (default: 8.0)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print each USB CDC command and received line.",
    )
    parser.add_argument(
        "--loop",
        type=int,
        default=1,
        help="Number of test iterations to run (default: 1, 0 means unlimited until --duration).",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0.0,
        help="Run soak mode for this many seconds (default: 0 disabled).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.loop < 0:
        print("[FAIL] --loop must be >= 0", file=sys.stderr)
        return 1
    if args.duration < 0:
        print("[FAIL] --duration must be >= 0", file=sys.stderr)
        return 1
    if args.loop == 0 and args.duration == 0:
        print("[FAIL] Use --duration when --loop is 0 (otherwise run is unbounded).", file=sys.stderr)
        return 1
    port = args.port or auto_detect_port()
    print(f"[INFO] Using port: {port}", flush=True)
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
            client = CdcClient(ser=ser, verbose=args.verbose)
            run_soak_test(
                client,
                timeout_s=args.timeout,
                startup_timeout_s=args.startup_timeout,
                loops=args.loop,
                duration_s=args.duration,
            )
    except SerialException as exc:
        print(
            f"[FAIL] {exc}\nHint: close any serial monitor using {port} and run again.",
            file=sys.stderr,
        )
        return 1
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    print("[PASS] USB CDC protocol smoke test passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
