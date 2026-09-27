#!/usr/bin/env python3
"""Contract checks for the read-only GPU sensor availability report."""

import importlib.util
import io
import json
import subprocess
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("sensor-availability.py")
SPEC = importlib.util.spec_from_file_location("sensor_availability", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class SensorAvailabilityTest(unittest.TestCase):
    def test_primary_unavailable_preserves_alternate_evidence(self):
        temperatures = [
            {"key": key, "celsius": value}
            for key, value in {
                "TCMz": 47.0,
                "Tg0D": None,
                "Tg05": 42.0,
                "Tg1B": 42.1,
                "TH0a": 30.0,
            }.items()
        ]
        response = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"schema": 1, "temperatures": temperatures}), stderr=""
        )
        with patch.object(AUDIT.subprocess, "run", return_value=response):
            reading = AUDIT.read_temperatures(Path("/unused/read-only-probe"))
        report = AUDIT.summarize([reading])
        self.assertEqual(report["unavailable"]["Tg0D"], 1)
        self.assertEqual(report["alternate_valid_when_Tg0D_unavailable"],
                         {"Tg05": 1, "Tg1B": 1})
        self.assertIsNone(report["valid_range_c"]["Tg0D"])
        self.assertEqual(report["valid_range_c"]["Tg05"], [42.0, 42.0])

    def test_missing_key_is_reported_as_unavailable(self):
        response = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"schema": 1, "temperatures": [
                {"key": "TCMz", "celsius": 48.0}
            ]}), stderr=""
        )
        with patch.object(AUDIT.subprocess, "run", return_value=response):
            reading = AUDIT.read_temperatures(Path("/unused/read-only-probe"))
        self.assertEqual(reading["TCMz"], 48.0)
        self.assertIsNone(reading.get("Tg0D"))
        self.assertEqual(AUDIT.summarize([reading])["absent_from_enumeration"]["Tg0D"], 1)
        event = AUDIT.sample_event(reading, 4, 4.1)
        self.assertFalse(event["selected_keys_enumerated"]["Tg0D"])
        self.assertIsNone(event["temperatures_c"]["Tg0D"])

    def test_simultaneous_gpu_gap_keeps_other_key_evidence_separate(self):
        readings = [{"TCMz": 48.0, "Tg0D": None, "Tg05": None,
                     "Tg1B": None, "Tg0L": 42.0, "TH0a": 30.0}]
        report = AUDIT.summarize(readings)
        self.assertEqual(report["alternate_valid_when_Tg0D_unavailable"],
                         {"Tg05": 0, "Tg1B": 0})
        self.assertEqual(report["other_Tg_keys_valid_when_Tg0D_unavailable"],
                         {"Tg0L": 1})
        self.assertEqual(report["absent_from_enumeration"]["Tg0D"], 0)

    def test_empty_enumeration_is_rejected(self):
        response = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"schema": 1, "temperatures": []}), stderr=""
        )
        with patch.object(AUDIT.subprocess, "run", return_value=response):
            with self.assertRaisesRegex(RuntimeError, "enumeration is unavailable"):
                AUDIT.read_temperatures(Path("/unused/read-only-probe"))

    def test_paired_reader_drops_to_original_user_for_second_child(self):
        response = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"schema": 1, "temperatures": [
                {"key": "Tg0D", "celsius": 42.0}
            ]}), stderr=""
        )
        with patch.object(AUDIT.subprocess, "run", return_value=response) as run:
            AUDIT.read_temperatures(Path("/unused/read-only-probe"), (501, 20))
        self.assertEqual(run.call_args.kwargs["user"], 501)
        self.assertEqual(run.call_args.kwargs["group"], 20)
        self.assertEqual(run.call_args.kwargs["extra_groups"], [])

    def test_paired_summary_distinguishes_root_and_user_gaps(self):
        pairs = [
            ({"Tg0D": None}, {"Tg0D": 43.0}),
            ({"Tg0D": None}, {"Tg0D": None}),
            ({"Tg0D": 42.0}, {"Tg0D": 43.0}),
        ]
        self.assertEqual(AUDIT.paired_summary(pairs)["Tg0D"], {
            "both_available": 1,
            "root_only_unavailable": 1,
            "user_only_unavailable": 0,
            "both_unavailable": 1,
        })

    def test_raw_reader_accepts_only_fixed_temperature_keys(self):
        readings = [{"key": key, "read_ok": True, "raw_celsius": 42.0}
                    for key in AUDIT.KEYS]
        response = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"schema": 1, "readings": readings}), stderr=""
        )
        with patch.object(AUDIT.subprocess, "run", return_value=response) as run:
            self.assertEqual(AUDIT.read_gpu_raw(Path("/unused/read-only-probe")), readings)
        self.assertEqual(run.call_args.args[0][-1], "--gpu-raw-json")

        missing = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"schema": 1, "readings": readings[:-1]}), stderr=""
        )
        with patch.object(AUDIT.subprocess, "run", return_value=missing):
            with self.assertRaisesRegex(RuntimeError, "unexpected schema"):
                AUDIT.read_gpu_raw(Path("/unused/read-only-probe"))

    def test_raw_probe_runs_only_after_filtered_gpu_gap(self):
        output = io.StringIO()
        normal = {"TCMz": 50.0, "Tg0D": 43.0, "Tg05": 43.1,
                  "Tg1B": 43.2, "TH0a": 31.0}
        gap = {**normal, "Tg0D": None, "Tg05": None, "Tg1B": None}
        with patch.object(AUDIT.os, "geteuid", return_value=0), \
                patch.dict(AUDIT.os.environ, {"SUDO_UID": "501"}), \
                patch.object(AUDIT, "RAW_GAP_SAMPLES", 2), \
                patch.object(AUDIT, "INTERVAL_SECONDS", 0), \
                patch.object(AUDIT, "read_temperatures", side_effect=[gap, normal]), \
                patch.object(AUDIT, "read_gpu_raw", return_value=[
                    {"key": "Tg0D", "read_ok": True, "raw_celsius": -1.95}
                ]) as raw, redirect_stdout(output):
            self.assertEqual(AUDIT.raw_gap_audit(Path("/unused"), "Mac15,7", "27.0"), 0)
        raw.assert_called_once()
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(events[1]["event"], "raw-gap")
        self.assertEqual(events[-1], {"event": "raw-gap-summary", "samples": 2, "gaps": 1})


if __name__ == "__main__":
    unittest.main()
