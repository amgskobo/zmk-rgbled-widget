#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
export MSYS_NO_PATHCONV=1
root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm@sha256:edb1c953438c6f720ddb79c3762f3972013b7fbbaf4fff3592fc869983e7afc5}"
docker run --rm --env ZMK_TEST_MODE="${ZMK_TEST_MODE:-quick}" --volume "$root:/src:ro" "$image" python3 /src/tests/runtime/run.py
