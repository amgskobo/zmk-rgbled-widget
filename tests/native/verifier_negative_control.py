# SPDX-License-Identifier: MIT
"""Require the original exit/marker-only predicate to fail both live controls."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_native_verifier as fixture


def exit_marker_only(result, required):
    # Original tests/native/run.py accepted exactly this condition.
    if result.returncode or any(marker not in result.stdout for marker in required):
        raise ValueError("native repeats failed")


fixture.validate_run = exit_marker_only
names = ["test_complete", "test_benign_settings",
         "test_actual_recovering_asan", "test_actual_recovering_ubsan"]
suite = unittest.TestSuite(fixture.NativeVerifier(name) for name in names)
result = unittest.TextTestRunner(verbosity=2).run(suite)
if (result.testsRun != 4 or result.errors or len(result.failures) != 2
        or any("ValueError not raised" not in text for _, text in result.failures)):
    raise SystemExit("unqualified exit/marker-only verifier control")
print("Original native predicate rejected by both live sanitizer assertions: PASS 2/2")
print("Original native predicate completion and benign-setting positives: PASS 2/2")
