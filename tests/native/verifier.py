# SPDX-License-Identifier: MIT
"""Admission for real-kernel runs, including recoverable sanitizer findings."""

import re


SANITIZER_DIAGNOSTIC = re.compile(
    rb"(?:AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|"
    rb"ThreadSanitizer|MemorySanitizer):|\bruntime error:",
    re.IGNORECASE,
)


def validate_run(result, required):
    """Require normal completion and clean diagnostics on both output streams."""
    if not required:
        raise ValueError("native completion markers must not be empty")
    if result.returncode:
        raise ValueError(f"native process exited {result.returncode}")
    if SANITIZER_DIAGNOSTIC.search(result.stdout + b"\n" + result.stderr):
        raise ValueError("native sanitizer diagnostic")
    missing = [marker.decode() for marker in required if marker not in result.stdout]
    if missing:
        raise ValueError(f"native completion markers missing: {missing}")
