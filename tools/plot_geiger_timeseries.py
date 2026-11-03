#!/usr/bin/env python3
"""Visualize Geiger correlated/uncorrelated counts over time from CSV log."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def load_series(path: Path) -> tuple[list[float], list[int], list[int], list[int]]:
    x_s: list[float] = []
    corr: list[int] = []
    unc1: list[int] = []
    unc2: list[int] = []

    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        base_ms: int | None = None
        for row in reader:
            try:
                t_ms = int(row["device_t_ms"])
                c = int(row["corr"])
                u1 = int(row["uncorr_ch1"])
                u2 = int(row["uncorr_ch2"])
            except (KeyError, TypeError, ValueError):
                continue
            if base_ms is None:
                base_ms = t_ms
            x_s.append((t_ms - base_ms) / 1000.0)
            corr.append(c)
            unc1.append(u1)
            unc2.append(u2)

    if not x_s:
        raise RuntimeError("No usable data rows found in CSV.")
    return x_s, corr, unc1, unc2


def cumulative(values: list[int]) -> list[int]:
    out: list[int] = []
    running = 0
    for v in values:
        running += v
        out.append(running)
    return out


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot Geiger event time series CSV.")
    parser.add_argument(
        "--input",
        default="tools/data/geiger_timeseries.csv",
        help="CSV input path (default: tools/data/geiger_timeseries.csv)",
    )
    parser.add_argument("--save", help="Optional output image path (e.g. tools/data/geiger_plot.png).")
    parser.add_argument("--no-show", action="store_true", help="Do not open interactive plot window.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_path = Path(args.input).resolve()
    if not input_path.exists():
        print(f"[FAIL] Input CSV not found: {input_path}", file=sys.stderr)
        return 1

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print(
            "[FAIL] matplotlib not installed. Install with: python3 -m pip install matplotlib",
            file=sys.stderr,
        )
        return 1

    try:
        x_s, corr, unc1, unc2 = load_series(input_path)
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1

    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

    axes[0].plot(x_s, corr, label="correlated", linewidth=1.6)
    axes[0].plot(x_s, unc1, label="uncorrelated ch1", linewidth=1.2)
    axes[0].plot(x_s, unc2, label="uncorrelated ch2", linewidth=1.2)
    axes[0].set_ylabel("counts / bin")
    axes[0].set_title("Geiger Event Rate Time Series")
    axes[0].grid(True, alpha=0.3)
    axes[0].legend()

    axes[1].plot(x_s, cumulative(corr), label="corr cumulative", linewidth=1.6)
    axes[1].plot(x_s, cumulative(unc1), label="uncorr ch1 cumulative", linewidth=1.2)
    axes[1].plot(x_s, cumulative(unc2), label="uncorr ch2 cumulative", linewidth=1.2)
    axes[1].set_xlabel("time since start (s)")
    axes[1].set_ylabel("cumulative counts")
    axes[1].set_title("Cumulative Event Counts")
    axes[1].grid(True, alpha=0.3)
    axes[1].legend()

    fig.tight_layout()

    if args.save:
        out_path = Path(args.save).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(out_path, dpi=150)
        print(f"[INFO] Saved plot: {out_path}", flush=True)

    if not args.no_show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
