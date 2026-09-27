#!/usr/bin/env python3
"""Contract checks for the read-only GPU sensor availability report."""

import importlib.util
import json
import subprocess
import unittest
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


if __name__ == "__main__":
    unittest.main()
