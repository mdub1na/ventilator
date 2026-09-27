#!/usr/bin/env python3
"""Read-only, bounded availability audit for the Mac15,7 temperature keys."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path


KEYS = ("TCMz", "Tg0D", "Tg05", "Tg1B", "TH0a")
SAMPLES = 120
INTERVAL_SECONDS = 1


def summarize(readings: list[dict[str, float | None]]) -> dict:
    missing = {key: 0 for key in KEYS}
    absent = {key: 0 for key in KEYS}
    valid = {key: [] for key in KEYS}
    fallback = {"Tg05": 0, "Tg1B": 0}
    other_tg = {}
    for reading in readings:
        for key in KEYS:
            value = reading.get(key)
            if value is None:
                missing[key] += 1
            else:
                valid[key].append(value)
            if key not in reading:
                absent[key] += 1
        if reading.get("Tg0D") is None:
            for key in fallback:
                if reading.get(key) is not None:
                    fallback[key] += 1
            for key, value in reading.items():
                if key.startswith("Tg") and key not in KEYS and value is not None:
                    other_tg[key] = other_tg.get(key, 0) + 1
    return {
        "event": "summary",
        "samples": len(readings),
        "unavailable": missing,
        "absent_from_enumeration": absent,
        "valid_range_c": {
            key: [min(values), max(values)] if values else None
            for key, values in valid.items()
        },
        "alternate_valid_when_Tg0D_unavailable": fallback,
        "other_Tg_keys_valid_when_Tg0D_unavailable": dict(sorted(other_tg.items())),
    }


def read_temperatures(probe: Path) -> dict[str, float | None]:
    result = subprocess.run(
        [str(probe), "--temperatures-json"],
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"read-only SMC probe exited {result.returncode}")
    data = json.loads(result.stdout)
    temperatures = data.get("temperatures")
    if data.get("schema") != 1 or not isinstance(temperatures, list) or not temperatures:
        raise RuntimeError("temperature enumeration is unavailable")
    found = {entry["key"]: entry["celsius"] for entry in temperatures}
    return found


def sample_event(reading: dict[str, float | None], index: int, elapsed: float) -> dict:
    sample = {"event": "sample", "index": index,
              "elapsed_seconds": round(elapsed, 2),
              "temperatures_c": {key: reading.get(key) for key in KEYS}}
    if reading.get("Tg0D") is None:
        sample["selected_keys_enumerated"] = {key: key in reading for key in KEYS}
        sample["other_Tg_keys_with_values"] = sorted(
            key for key, value in reading.items()
            if key.startswith("Tg") and key not in KEYS and value is not None
        )
    return sample


def main() -> int:
    if len(sys.argv) != 1:
        print("Usage: sensor-availability.py", file=sys.stderr)
        return 2
    probe = Path(__file__).with_name("smc-read").resolve()
    if not probe.is_file():
        print("Build prototype/smc-read/smc-read first", file=sys.stderr)
        return 2
    try:
        model = subprocess.run(
            ["sysctl", "-n", "hw.model"], capture_output=True, text=True, check=True
        ).stdout.strip()
        macos = subprocess.run(
            ["sw_vers", "-productVersion"], capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        print("Cannot verify the Mac model and macOS version", file=sys.stderr)
        return 2
    if model != "Mac15,7" or macos != "27.0":
        print(f"Read-only audit is limited to Mac15,7/macOS 27.0; found {model}/{macos}",
              file=sys.stderr)
        return 2
    print(json.dumps({"event": "start", "model": model, "macOS": macos,
                      "root": os.geteuid() == 0, "planned_samples": SAMPLES}), flush=True)
    readings = []
    started = time.monotonic()
    try:
        for index in range(SAMPLES):
            delay = started + index * INTERVAL_SECONDS - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            reading = read_temperatures(probe)
            readings.append(reading)
            if index == 0 or index == SAMPLES - 1 or any(
                reading.get(key) is None for key in KEYS
            ):
                print(json.dumps(sample_event(
                    reading, index, time.monotonic() - started
                )), flush=True)
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"event": "read_failed", "index": len(readings),
                          "reason": str(error)}), file=sys.stderr, flush=True)
        print(json.dumps(summarize(readings)), flush=True)
        return 1
    except KeyboardInterrupt:
        print(json.dumps(summarize(readings)), flush=True)
        return 130
    print(json.dumps(summarize(readings)), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
