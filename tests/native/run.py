#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the complete widget with real Zephyr kernel/LED/GPIO-emulator APIs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--zephyr", type=Path, required=True)
parser.add_argument("--cmsis", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--jobs", type=int, default=2)
parser.add_argument(
    "--wide-indices",
    action="store_true",
    help="exercise RGB children after 256 unrelated siblings",
)
parser.add_argument(
    "--layer-pulse",
    action="store_true",
    help="disable automatic status and verify one-shot layer colors",
)
parser.add_argument(
    "--ignore-aml",
    action="store_true",
    help="exercise ignored automatic layer ID9 with --layer-pulse",
)
parser.add_argument(
    "--status-only",
    action="store_true",
    help="retain status indications with both layer modes disabled",
)
parser.add_argument(
    "--peripheral",
    action="store_true",
    help="exercise split peripheral status with --status-only",
)
parser.add_argument(
    "--hide-usb",
    action="store_true",
    help="show the BLE profile state while USB is selected",
)
args = parser.parse_args()
if args.ignore_aml and not args.layer_pulse:
    parser.error("--ignore-aml requires --layer-pulse")
if args.peripheral and not args.status_only:
    parser.error("--peripheral requires --status-only")
if args.layer_pulse and (args.status_only or args.hide_usb):
    parser.error("--layer-pulse has its own quiet status contract")
root = Path(__file__).resolve().parents[2]
out = args.output
if not out.is_absolute() or out.exists() or out.is_symlink():
    parser.error("output must be a fresh absolute path")
if args.jobs < 1 or out.resolve().is_relative_to(root):
    parser.error("select a positive compiler budget and evidence outside source")
out.mkdir(parents=True)
identities = []
for path in (args.zephyr, args.cmsis):
    identities.append(
        dict(
            path=str(path),
            revision=subprocess.check_output(
                ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
            ).strip(),
        )
    )
(out / "dependencies.json").write_text(json.dumps(identities, indent=2))
build = out / "build"
include = build / "include"
headers = set()
for source in (root / "src/widget.c", root / "src/behaviors/behavior_rgbled_widget.c"):
    headers.update(re.findall(r"#include <([^>]+)>", source.read_text()))
for name in headers:
    if name.startswith(("zmk/", "drivers/behavior")):
        path = include / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "' + str(root / "tests/native/boundaries.h") + '"\n')
colors = include / "layer-colors.h"
colors.parent.mkdir(parents=True, exist_ok=True)
colors.write_text(
    "\n".join(
        f"#define CONFIG_RGBLED_WIDGET_LAYER_{i}_COLOR {i if i < 8 else 0}"
        for i in range(32)
    )
    + "\n"
)
env = dict(
    os.environ,
    ZEPHYR_BASE=str(args.zephyr),
    Zephyr_DIR=str(args.zephyr / "share/zephyr-package/cmake"),
)
command = [
    "west",
    "build",
    "-o=-j" + str(args.jobs),
    "-s",
    str(root / "tests/native"),
    "-d",
    str(build),
    "-b",
    "native_sim/native/64",
    "--",
    "-DZEPHYR_MODULES=" + str(args.cmsis),
]
if args.layer_pulse:
    command.append("-DRGB_WIDGET_LAYER_PULSE_TEST=ON")
if args.ignore_aml:
    command.append("-DRGB_WIDGET_IGNORE_AML_TEST=ON")
for enabled, option in (
    (args.status_only, "STATUS_ONLY"),
    (args.peripheral, "PERIPHERAL"),
    (args.hide_usb, "HIDE_USB"),
):
    if enabled:
        command.append("-DRGB_WIDGET_" + option + "_TEST=ON")
if args.wide_indices:
    overlay = out / "wide-indices.overlay"
    children = "\n".join(
        f"        unused_{i} {{ gpios = <&gpio0 0 GPIO_ACTIVE_HIGH>; }};"
        for i in range(256)
    )
    overlay.write_text(
        "/ {\n    aliases { led-red = &wide_red; led-green = &wide_green; "
        "led-blue = &wide_blue; };\n    wide_rgb_leds {\n"
        '        compatible = "gpio-leds";\n' + children + "\n"
        "        wide_red: red { gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>; };\n"
        "        wide_green: green { gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>; };\n"
        "        wide_blue: blue { gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>; };\n"
        "    };\n};\n"
    )
    command.append("-DEXTRA_DTC_OVERLAY_FILE=" + str(overlay))
(out / "command.json").write_text(json.dumps(command, indent=2))
with (out / "build.log").open("w") as stream:
    result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
if result.returncode:
    print((out / "build.log").read_text()[-6000:])
    raise SystemExit(result.returncode)
binary = build / "zephyr/zephyr.exe"
(out / "binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest())
failed = []
reports = []
for repeat in range(1, 4):
    started = time.time_ns()
    clock = time.monotonic_ns()
    result = subprocess.run([str(binary)], capture_output=True, timeout=30)
    (out / f"run-{repeat}.stdout").write_bytes(result.stdout)
    (out / f"run-{repeat}.stderr").write_bytes(result.stderr)
    (out / f"run-{repeat}.exit").write_text(str(result.returncode) + "\n")
    reports.append(
        dict(
            repeat=repeat,
            argv=[str(binary)],
            start_unix_ns=started,
            elapsed_ns=time.monotonic_ns() - clock,
            exit=result.returncode,
        )
    )
    (out / "runs.json").write_text(json.dumps(reports, indent=2) + "\n")
    required = [b"RGB_NATIVE_COMPLETE"]
    if not args.layer_pulse:
        required += [
            b"RGB_CONNECTIVITY_SLEEP_COMPLETE",
            b"RGB_ENDPOINT_COLORS_COMPLETE",
        ]
    if result.returncode or any(marker not in result.stdout for marker in required):
        failed.append(dict(repeat=repeat, exit=result.returncode))
if failed:
    raise SystemExit(f"native repeats failed: {failed}; raw logs retained")
for fixture, diagnostic in (
    ()
    if args.wide_indices or args.layer_pulse or args.status_only or args.hide_usb
    else (
        ("mixed-parent", "RGBLED_WIDGET LED aliases must share one gpio-leds parent"),
        ("wrong-parent", "RGBLED_WIDGET LED aliases must belong to a gpio-leds device"),
    )
):
    guard = out / fixture
    guard.mkdir()
    guard_command = [
        "west",
        "build",
        "-o=-j" + str(args.jobs),
        "-s",
        str(root / "tests/native"),
        "-d",
        str(guard / "build"),
        "-b",
        "native_sim/native/64",
        "--",
        "-DZEPHYR_MODULES=" + str(args.cmsis),
        "-DEXTRA_DTC_OVERLAY_FILE=" + str(root / "tests/native" / f"{fixture}.overlay"),
    ]
    # The normal build generated the controlled API headers above. Reuse those
    # exact headers; each guard still configures/builds its own real DTS graph.
    guard_env = dict(env, CFLAGS="-I" + str(include))
    (guard / "command.json").write_text(json.dumps(guard_command, indent=2))
    with (guard / "build.log").open("w") as stream:
        result = subprocess.run(
            guard_command, env=guard_env, stdout=stream, stderr=subprocess.STDOUT
        )
    (guard / "exit").write_text(str(result.returncode) + "\n")
    if not result.returncode or diagnostic not in (guard / "build.log").read_text():
        raise SystemExit(
            f"{fixture}: intended compiler rejection missing; raw log retained"
        )
    print(f"{fixture}: exact compiler rejection PASS")
print(
    "RGB real kernel lifecycle PASS 3/3 and alias guards PASS; controlled ZMK inputs, no hardware claim"
)
