#!/usr/bin/env python3
"""Continuously poll RP2040 Geiger stats and persist full time series to CSV."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
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
        raise RuntimeError("Serial write failed after retries.") from last_exc

    def read_line(self, timeout_s: float) -> str:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                raw = self.ser.readline()
            except SerialException as exc:
                raise RuntimeError(f"Serial read failed: {exc}") from exc
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
        raise RuntimeError(f"Device returned failed ack: {ack}")


def wait_for_status(client: CdcClient, startup_timeout_s: float) -> None:
    print(f"[INFO] Waiting for controller status (up to {startup_timeout_s:.0f}s)...", flush=True)
    deadline = time.monotonic() + startup_timeout_s
    while time.monotonic() < deadline:
        try:
            payload = client.read_json(0.8, expected_type="status")
            if payload.get("type") == "status":
                print("[INFO] Controller is responding.", flush=True)
                return
        except TimeoutError:
            continue
    raise TimeoutError("No status JSON seen. Close serial monitor and retry.")


def request_geiger_stats(client: CdcClient, timeout_s: float) -> dict[str, Any]:
    client.write_line("GET GEIGER_STATS")
    stats = client.read_json(timeout_s=timeout_s, expected_type="geiger_stats")
    expect_ok_ack(client, timeout_s=timeout_s)
    return stats


def ensure_csv(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        return
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "host_time_iso",
                "host_time_s",
                "device_t_ms",
                "corr",
                "uncorr_ch1",
                "uncorr_ch2",
                "total_corr",
                "total_uncorr_ch1",
                "total_uncorr_ch2",
                "total_ch1",
                "total_ch2",
            ]
        )


def detect_last_device_t_ms(path: Path) -> int | None:
    if not path.exists():
        return None
    last_value: int | None = None
    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            value = row.get("device_t_ms")
            if value is None:
                continue
            try:
                last_value = int(value)
            except ValueError:
                continue
    return last_value


def append_new_points(path: Path, stats: dict[str, Any], last_t_ms: int | None) -> tuple[int, int | None]:
    series = stats.get("series", [])
    if not isinstance(series, list):
        raise RuntimeError("Malformed geiger_stats: 'series' is not a list")

    total = stats.get("total", {})
    if not isinstance(total, dict):
        raise RuntimeError("Malformed geiger_stats: 'total' is not an object")

    points: list[dict[str, int]] = []
    for point in series:
        if not isinstance(point, dict):
            continue
        try:
            t_ms = int(point["t_ms"])
            corr = int(point["corr"])
            unc1 = int(point["uncorr_ch1"])
            unc2 = int(point["uncorr_ch2"])
        except (KeyError, TypeError, ValueError):
            continue
        if last_t_ms is not None and t_ms <= last_t_ms:
            continue
        points.append({"t_ms": t_ms, "corr": corr, "unc1": unc1, "unc2": unc2})

    if not points:
        return 0, last_t_ms

    points.sort(key=lambda x: x["t_ms"])
    now = datetime.now(timezone.utc)
    host_iso = now.isoformat()
    host_s = f"{now.timestamp():.6f}"

    with path.open("a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        for p in points:
            writer.writerow(
                [
                    host_iso,
                    host_s,
                    p["t_ms"],
                    p["corr"],
                    p["unc1"],
                    p["unc2"],
                    int(total.get("corr", 0)),
                    int(total.get("uncorr_ch1", 0)),
                    int(total.get("uncorr_ch2", 0)),
                    int(total.get("ch1", 0)),
                    int(total.get("ch2", 0)),
                ]
            )
    return len(points), points[-1]["t_ms"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Poll GET GEIGER_STATS forever and log full time series.")
    parser.add_argument("--port", help="Serial port (e.g. /dev/ttyACM0). Auto-detected if omitted.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="Read timeout in seconds (default: 2.0)")
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=12.0,
        help="Time to wait for first status JSON after opening port (default: 12.0)",
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=1.0,
        help="Polling interval in seconds (default: 1.0)",
    )
    parser.add_argument(
        "--output",
        default="tools/data/geiger_timeseries.csv",
        help="CSV output file path (default: tools/data/geiger_timeseries.csv)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print raw command/response serial lines.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.poll_interval <= 0:
        print("[FAIL] --poll-interval must be > 0", file=sys.stderr)
        return 1

    out_path = Path(args.output).resolve()
    ensure_csv(out_path)
    last_t_ms = detect_last_device_t_ms(out_path)
    if last_t_ms is not None:
        print(f"[INFO] Continuing log after device_t_ms={last_t_ms}", flush=True)

    port = args.port or auto_detect_port()
    print(f"[INFO] Using port: {port}", flush=True)
    print(f"[INFO] Writing CSV: {out_path}", flush=True)

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
            wait_for_status(client, args.startup_timeout)

            polls = 0
            written_total = 0
            while True:
                stats = request_geiger_stats(client, timeout_s=args.timeout)
                written, last_t_ms = append_new_points(out_path, stats, last_t_ms)
                polls += 1
                written_total += written
                if written > 0 or polls % 10 == 0:
                    print(
                        f"[INFO] poll={polls} wrote={written} total_written={written_total}",
                        flush=True,
                    )
                time.sleep(args.poll_interval)
    except KeyboardInterrupt:
        print("\n[INFO] Stopped by user.", flush=True)
        return 0
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
