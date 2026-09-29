"""Verify that the diagnostic reads the executable, not a spoofed argv[0]."""

import subprocess
import unittest
from pathlib import Path


HELPER = Path(__file__).with_name("helper-status")
RUNNER = "/private/var/db/com.ventilator.supervisor-read-only/supervisor-executable-v1"


class ProcessPathTest(unittest.TestCase):
    def test_exited_process_has_no_path(self):
        child = subprocess.Popen(["/usr/bin/true"])
        child.wait(timeout=5)
        result = subprocess.run(
            [str(HELPER), "process-path", str(child.pid)],
            capture_output=True, text=True, timeout=5,
        )
        self.assertEqual(1, result.returncode)
        self.assertEqual("", result.stdout)
        absent = subprocess.run([str(HELPER), "process-absent", str(child.pid)], timeout=5)
        self.assertEqual(0, absent.returncode)

    def test_spoofed_runner_argv_does_not_pass_as_the_executable(self):
        with subprocess.Popen([RUNNER, "15"], executable="/bin/sleep") as child:
            try:
                result = subprocess.run(
                    [str(HELPER), "process-path", str(child.pid)],
                    capture_output=True, text=True, timeout=5, check=True,
                )
                self.assertEqual("/bin/sleep", result.stdout.strip())
                self.assertNotEqual(RUNNER, result.stdout.strip())
                absent = subprocess.run([str(HELPER), "process-absent", str(child.pid)], timeout=5)
                self.assertEqual(1, absent.returncode)
            finally:
                child.terminate()
                child.wait(timeout=5)

    def test_invalid_pid_is_rejected(self):
        for mode in ("process-path", "process-absent"):
            for pid in ("", "0", "-1", "1x", "2147483648"):
                with self.subTest(mode=mode, pid=pid):
                    result = subprocess.run(
                        [str(HELPER), mode, pid],
                        capture_output=True, text=True, timeout=5,
                    )
                    self.assertEqual(2, result.returncode)
                    self.assertEqual("", result.stdout)


if __name__ == "__main__":
    unittest.main()
