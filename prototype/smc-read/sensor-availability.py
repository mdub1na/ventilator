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
PAIRED_SAMPLES = 60
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


def read_temperatures(
    probe: Path, identity: tuple[int, int] | None = None
) -> dict[str, float | None]:
    process_identity = (
        {"user": identity[0], "group": identity[1], "extra_groups": []}
        if identity is not None else {}
    )
    result = subprocess.run(
        [str(probe), "--temperatures-json"],
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
        **process_identity,
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


def pair_classification(root: dict, user: dict) -> str:
    root_missing = root.get("Tg0D") is None
    user_missing = user.get("Tg0D") is None
    if root_missing and user_missing:
        return "both_unavailable"
    if root_missing:
        return "root_only_unavailable"
    if user_missing:
        return "user_only_unavailable"
    return "both_available"


def paired_summary(pairs: list[tuple[dict, dict]]) -> dict:
    counts = {name: 0 for name in (
        "both_available", "root_only_unavailable", "user_only_unavailable",
        "both_unavailable"
    )}
    for root, user in pairs:
        counts[pair_classification(root, user)] += 1
    return {"event": "paired-summary", "pairs": len(pairs), "Tg0D": counts}


def paired_audit(probe: Path, model: str, macos: str) -> int:
    try:
        uid = int(os.environ["SUDO_UID"])
        gid = int(os.environ["SUDO_GID"])
    except (KeyError, ValueError):
        print("Paired audit requires sudo from a regular user", file=sys.stderr)
        return 2
    if os.geteuid() != 0 or uid <= 0 or gid < 0:
        print("Paired audit requires sudo from a regular user", file=sys.stderr)
        return 2
    print(json.dumps({"event": "paired-start", "model": model, "macOS": macos,
                      "planned_pairs": PAIRED_SAMPLES, "user_child_unprivileged": True}),
          flush=True)
    pairs = []
    started = time.monotonic()
    current_side = "root"
    try:
        for index in range(PAIRED_SAMPLES):
            delay = started + index * INTERVAL_SECONDS - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            order = ("root", "user") if index % 2 == 0 else ("user", "root")
            readings = {}
            read_started = {}
            for current_side in order:
                read_started[current_side] = time.monotonic()
                identity = None if current_side == "root" else (uid, gid)
                readings[current_side] = read_temperatures(probe, identity)
            root, user = readings["root"], readings["user"]
            pairs.append((root, user))
            classification = pair_classification(root, user)
            if index == 0 or index == PAIRED_SAMPLES - 1 or classification != "both_available":
                print(json.dumps({
                    "event": "pair", "index": index,
                    "elapsed_seconds": round(time.monotonic() - started, 2),
                    "order": list(order),
                    "read_start_gap_ms": round(abs(
                        read_started["root"] - read_started["user"]
                    ) * 1000),
                    "classification": classification,
                    "temperatures_c": {
                        side: {key: readings[side].get(key) for key in KEYS}
                        for side in order
                    },
                    "Tg_keys_with_values": {
                        side: sorted(key for key, value in readings[side].items()
                                     if key.startswith("Tg") and value is not None)
                        for side in order
                    },
                }), flush=True)
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"event": "paired-read-failed", "index": len(pairs),
                          "side": current_side, "reason": str(error)}),
              file=sys.stderr, flush=True)
        print(json.dumps(paired_summary(pairs)), flush=True)
        return 1
    except KeyboardInterrupt:
        print(json.dumps(paired_summary(pairs)), flush=True)
        return 130
    print(json.dumps(paired_summary(pairs)), flush=True)
    return 0


def main() -> int:
    paired = len(sys.argv) == 2 and sys.argv[1] == "--paired"
    if len(sys.argv) != 1 and not paired:
        print("Usage: sensor-availability.py [--paired]", file=sys.stderr)
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
    if paired:
        return paired_audit(probe, model, macos)
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
