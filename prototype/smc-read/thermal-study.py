#!/usr/bin/env python3
"""Bounded, read-only SMC snapshots before, during, and after CPU work."""

import argparse
import json
import multiprocessing
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


TEMPERATURE = re.compile(r"^  (\S{4})\s+([0-9]+(?:\.[0-9]+)?) C \(type", re.M)
FAN_SPEED = re.compile(r"^  (F[0-9]Ac)\s+([0-9]+(?:\.[0-9]+)?)\s+RPM", re.M)


def cpu_work(stop_at: float) -> None:
    value = 1
    while time.monotonic() < stop_at:
        value = (value * 1664525 + 1013904223) & 0xFFFFFFFF


def snapshot(probe: Path, phase: str) -> dict:
    output = subprocess.run(
        [str(probe), "--all-temperatures"],
        check=True,
        capture_output=True,
        text=True,
        timeout=15,
    ).stdout
    temperatures = {key: float(value) for key, value in TEMPERATURE.findall(output)}
    fans = {key: float(value) for key, value in FAN_SPEED.findall(output)}
    if not temperatures or not fans:
        raise RuntimeError("SMC snapshot did not contain temperatures and fan speeds")
    return {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "phase": phase,
        "temperatures_c": temperatures,
        "fan_rpm": fans,
    }


def record(probe: Path, output_file, phase: str) -> None:
    reading = snapshot(probe, phase)
    output_file.write(json.dumps(reading, sort_keys=True) + "\n")
    output_file.flush()
    print(f"{reading['timestamp_utc']} {phase}: {len(reading['temperatures_c'])} sensors", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4, choices=range(1, 5))
    parser.add_argument("--load-seconds", type=int, default=30, choices=range(10, 61))
    args = parser.parse_args()

    probe = Path(__file__).with_name("smc-read").resolve()
    if not probe.is_file():
        parser.error("build smc-read first with `make build`")
    workers = []
    try:
        with args.output.open("w", encoding="utf-8") as output_file:
            for _ in range(3):
                record(probe, output_file, "idle")
                time.sleep(2)

            stop_at = time.monotonic() + args.load_seconds
            workers = [
                multiprocessing.Process(target=cpu_work, args=(stop_at,), daemon=True)
                for _ in range(args.workers)
            ]
            for worker in workers:
                worker.start()
            while time.monotonic() < stop_at:
                record(probe, output_file, "cpu_load")
                time.sleep(4)
            for worker in workers:
                worker.join(timeout=2)

            for _ in range(3):
                record(probe, output_file, "recovery")
                time.sleep(4)
    finally:
        for worker in workers:
            if worker.is_alive():
                worker.terminate()
                worker.join(timeout=2)


if __name__ == "__main__":
    main()
