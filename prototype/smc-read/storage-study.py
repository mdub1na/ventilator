#!/usr/bin/env python3
"""Collect read-only SMC snapshots around a bounded local SSD workload."""

import argparse
import fcntl
import json
import multiprocessing
import os
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


MIB = 1024 * 1024
GIB = 1024 * MIB
BLOCK_SIZE = MIB
F_NOCACHE = 48  # Darwin fcntl command; keeps workload reads out of the file cache.


def disable_file_cache(file_object) -> None:
    fcntl.fcntl(file_object.fileno(), F_NOCACHE, 1)


def storage_work(
    path: Path,
    stop_at: float,
    file_size_bytes: int,
    max_written_bytes: int,
    written_bytes,
    read_bytes,
) -> None:
    payload = bytes(range(256)) * 4096
    try:
        while time.monotonic() < stop_at and written_bytes.value < max_written_bytes:
            bytes_in_file = 0
            with path.open("wb", buffering=0) as output_file:
                disable_file_cache(output_file)
                while bytes_in_file < file_size_bytes and time.monotonic() < stop_at:
                    count = min(len(payload), file_size_bytes - bytes_in_file)
                    written = output_file.write(payload[:count])
                    if written <= 0:
                        raise OSError("storage workload write made no progress")
                    bytes_in_file += written
                    with written_bytes.get_lock():
                        written_bytes.value += written
                os.fsync(output_file.fileno())

        while time.monotonic() < stop_at and path.exists():
            with path.open("rb", buffering=0) as input_file:
                disable_file_cache(input_file)
                while time.monotonic() < stop_at:
                    chunk = input_file.read(BLOCK_SIZE)
                    if not chunk:
                        break
                    with read_bytes.get_lock():
                        read_bytes.value += len(chunk)
    finally:
        path.unlink(missing_ok=True)


def snapshot(probe: Path, phase: str, written_bytes: int, read_bytes: int) -> dict:
    completed = subprocess.run(
        [str(probe), "--temperatures-json"],
        check=True,
        capture_output=True,
        text=True,
        timeout=20,
    )
    document = json.loads(completed.stdout)
    readings = document.get("temperatures")
    if not isinstance(readings, list):
        raise RuntimeError("SMC snapshot did not contain a temperature list")
    temperatures = {
        reading["key"]: reading["celsius"]
        for reading in readings
        if isinstance(reading.get("key"), str) and reading.get("celsius") is not None
    }
    if not temperatures:
        raise RuntimeError("SMC snapshot did not contain temperature values")
    return {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "phase": phase,
        "temperatures_c": temperatures,
        "io_bytes": {"written": written_bytes, "read": read_bytes},
    }


def record(probe: Path, output_file, phase: str, written_bytes, read_bytes) -> None:
    reading = snapshot(probe, phase, written_bytes.value, read_bytes.value)
    output_file.write(json.dumps(reading, sort_keys=True) + "\n")
    output_file.flush()
    th0a = reading["temperatures_c"].get("TH0a")
    print(
        f"{reading['timestamp_utc']} {phase}: "
        f"TH0a={th0a} C, written={reading['io_bytes']['written'] / GIB:.2f} GiB, "
        f"read={reading['io_bytes']['read'] / GIB:.2f} GiB",
        flush=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--load-seconds", type=int, default=45, choices=range(20, 91))
    parser.add_argument("--file-size-mib", type=int, default=512, choices=(128, 256, 512))
    parser.add_argument("--max-written-gib", type=int, default=4, choices=range(1, 9))
    args = parser.parse_args()

    probe = Path(__file__).with_name("smc-read").resolve()
    if not probe.is_file():
        parser.error("build smc-read first with `make build`")
    workload_file = Path("/private/tmp/ventilator-storage-study.bin")
    if args.output.resolve() == workload_file:
        parser.error("output path must differ from the temporary workload file")
    if workload_file.exists():
        parser.error(f"temporary workload file already exists: {workload_file}")

    written_bytes = multiprocessing.Value("Q", 0)
    read_bytes = multiprocessing.Value("Q", 0)
    worker = None
    try:
        with args.output.open("w", encoding="utf-8") as output_file:
            for _ in range(3):
                record(probe, output_file, "idle", written_bytes, read_bytes)
                time.sleep(2)

            stop_at = time.monotonic() + args.load_seconds
            worker = multiprocessing.Process(
                target=storage_work,
                args=(
                    workload_file,
                    stop_at,
                    args.file_size_mib * MIB,
                    args.max_written_gib * GIB,
                    written_bytes,
                    read_bytes,
                ),
            )
            worker.start()
            while time.monotonic() < stop_at:
                record(probe, output_file, "storage_load", written_bytes, read_bytes)
                time.sleep(4)
            worker.join(timeout=10)
            if worker.exitcode != 0:
                raise RuntimeError(f"storage workload exited with code {worker.exitcode}")

            for _ in range(4):
                record(probe, output_file, "recovery", written_bytes, read_bytes)
                time.sleep(4)
    finally:
        if worker is not None and worker.is_alive():
            worker.terminate()
            worker.join(timeout=2)
        workload_file.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
