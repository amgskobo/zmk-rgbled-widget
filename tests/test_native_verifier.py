# SPDX-License-Identifier: MIT
"""Recovering sanitizer processes must not pass the native clean gate."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import json

from native.verifier import validate_run


MARKERS = [b"RGB_NATIVE_COMPLETE", b"RGB_CONNECTIVITY_SLEEP_COMPLETE",
           b"RGB_ENDPOINT_COLORS_COMPLETE"]
COMPLETE = b"\n".join(MARKERS) + b"\n"


class NativeVerifier(unittest.TestCase):
    def result(self, stdout=COMPLETE, stderr=b"", code=0):
        return subprocess.CompletedProcess(["synthetic-native"], code, stdout, stderr)

    def test_complete(self):
        validate_run(self.result(), MARKERS)

    def test_nonzero(self):
        with self.assertRaisesRegex(ValueError, "process exited"):
            validate_run(self.result(code=1), MARKERS)

    def test_missing_marker(self):
        with self.assertRaisesRegex(ValueError, "markers missing"):
            validate_run(self.result(stdout=MARKERS[0]), MARKERS)

    def test_stderr_marker_does_not_complete(self):
        with self.assertRaisesRegex(ValueError, "markers missing"):
            validate_run(self.result(stdout=b"", stderr=COMPLETE), MARKERS)

    def test_empty_markers(self):
        with self.assertRaisesRegex(ValueError, "must not be empty"):
            validate_run(self.result(), [])

    def test_asan_stdout(self):
        with self.assertRaisesRegex(ValueError, "sanitizer diagnostic"):
            validate_run(self.result(stdout=COMPLETE + b"ERROR: AddressSanitizer: overflow"), MARKERS)

    def test_asan_stderr(self):
        with self.assertRaisesRegex(ValueError, "sanitizer diagnostic"):
            validate_run(self.result(stderr=b"AddressSanitizer:DEADLYSIGNAL"), MARKERS)

    def test_ubsan_stdout(self):
        with self.assertRaisesRegex(ValueError, "sanitizer diagnostic"):
            validate_run(self.result(stdout=COMPLETE + b"fixture.c:1:2: runtime error: shift"), MARKERS)

    def test_ubsan_stderr(self):
        with self.assertRaisesRegex(ValueError, "sanitizer diagnostic"):
            validate_run(self.result(stderr=b"UndefinedBehaviorSanitizer: invalid load"), MARKERS)

    def test_leak_stderr(self):
        with self.assertRaisesRegex(ValueError, "sanitizer diagnostic"):
            validate_run(self.result(stderr=b"ERROR: LeakSanitizer: memory leaks"), MARKERS)

    def test_benign_settings(self):
        validate_run(self.result(stderr=b"ASAN_OPTIONS=halt_on_error=0; UBSAN_OPTIONS=print_stacktrace=1"), MARKERS)
        validate_run(self.result(stdout=COMPLETE + b"sanitizer configuration: address,undefined\n"), MARKERS)

    def recovering_process(self, label, expression, flags, expected):
        with tempfile.TemporaryDirectory(prefix="rgb-native-verifier-") as folder:
            source = Path(folder) / (label + ".c")
            binary = Path(folder) / label
            source.write_text(
                "#include <limits.h>\n#include <stdio.h>\nint main(void) {\n"
                + expression
                + '\nputs("RGB_NATIVE_COMPLETE synthetic control");\n'
                + 'puts("RGB_CONNECTIVITY_SLEEP_COMPLETE synthetic control");\n'
                + 'puts("RGB_ENDPOINT_COLORS_COMPLETE synthetic control");\nreturn 0;\n}\n'
            )
            subprocess.run(["cc", "-O0", "-g", "-fno-pie", "-no-pie", *flags,
                            str(source), "-o", str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], capture_output=True, timeout=10,
                                    env=dict(os.environ, ASAN_OPTIONS="halt_on_error=0:detect_leaks=0",
                                             UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=0"))
            # These are control prerequisites outside the verifier under test.
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(expected, result.stderr)
            for marker in MARKERS:
                self.assertIn(marker, result.stdout)
            print(json.dumps({"control": label, "exit": result.returncode,
                              "stdout": result.stdout.decode(errors="replace"),
                              "stderr": result.stderr.decode(errors="replace")}), flush=True)
            with self.assertRaisesRegex(ValueError, "sanitizer diagnostic"):
                validate_run(result, MARKERS)

    def test_actual_recovering_ubsan(self):
        self.recovering_process("ubsan", "volatile int a = INT_MAX, b = 1; volatile int c = a + b; (void)c;",
                                ["-fsanitize=undefined", "-fsanitize-recover=undefined"], b"runtime error:")

    def test_actual_recovering_asan(self):
        self.recovering_process("asan", "volatile int i = 2; int values[1] = {0}; volatile int c = values[i]; (void)c;",
                                ["-fsanitize=address", "-fsanitize-recover=address"], b"ERROR: AddressSanitizer:")


if __name__ == "__main__":
    unittest.main()
