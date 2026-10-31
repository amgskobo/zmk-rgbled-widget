#!/usr/bin/env python3
"""Compile both complete production sources; only external APIs are controlled."""
import os
import json
import shutil
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = {
    "central-colors": (1, 1, 0, 1, 1, 0),
    "central-blinks": (1, 1, 1, 0, 1, 0),
    "peripheral": (1, 0, 1, 1, 1, 0),
    "usb-hidden": (0, 0, 0, 0, 0, 0),
    "usb-only": (0, 0, 0, 0, 1, 0),
    "all-batteries": (1, 1, 0, 0, 1, 1),
    "peripheral-batteries": (1, 1, 0, 0, 1, 2),
    "quiet-pulse-central": (1, 1, 1, 0, 1, 0),
    "quiet-pulse-usb": (0, 0, 1, 0, 1, 0),
    "quiet-pulse-peripheral": (1, 0, 1, 0, 1, 0),
    "quiet-pulse-ignore-aml": (1, 1, 1, 0, 1, 0),
    "quiet-pulse-ignore-aml-usb": (0, 0, 1, 0, 1, 0),
}

def main():
    mode = os.environ.get("ZMK_TEST_MODE", "quick")
    if mode not in ("quick", "full"):
        raise SystemExit("ZMK_TEST_MODE must be quick or full")
    headers = set()
    for source in (ROOT / "src/widget.c", ROOT / "src/behaviors/behavior_rgbled_widget.c"):
        import re
        headers.update(re.findall(r"#include <([^>]+)>", source.read_text()))
    headers.remove("zmk_rgbled_widget/widget.h")
    with tempfile.TemporaryDirectory(prefix="rgb-widget-") as folder:
        work = Path(folder)
        for name in headers:
            path = work / "include" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "stubs.h"\n')
        config = {
            "RGBLED_WIDGET": 1, "ZMK_USB": 1, "ZMK_BATTERY_REPORTING": 1,
            "ZMK_SPLIT_BLE": 1, "ZMK_BEHAVIOR_METADATA": 0, "ZMK_SPLIT_BLE_PERIPHERAL_COUNT": 2,
            "RGBLED_WIDGET_INTERVAL_MS": 500, "RGBLED_WIDGET_CONN_BLINK_MS": 1000,
            "RGBLED_WIDGET_CONN_COLOR_USB": 6, "RGBLED_WIDGET_CONN_COLOR_CONNECTED": 4,
            "RGBLED_WIDGET_CONN_COLOR_ADVERTISING": 3, "RGBLED_WIDGET_CONN_COLOR_DISCONNECTED": 1,
            "RGBLED_WIDGET_BATTERY_BLINK_MS": 2000, "RGBLED_WIDGET_BATTERY_LEVEL_HIGH": 80,
            "RGBLED_WIDGET_BATTERY_LEVEL_LOW": 20, "RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL": 5,
            "RGBLED_WIDGET_BATTERY_COLOR_HIGH": 2, "RGBLED_WIDGET_BATTERY_COLOR_MEDIUM": 3,
            "RGBLED_WIDGET_BATTERY_COLOR_LOW": 1, "RGBLED_WIDGET_BATTERY_COLOR_CRITICAL": 1,
            "RGBLED_WIDGET_BATTERY_COLOR_MISSING": 5, "RGBLED_WIDGET_LAYER_BLINK_MS": 100,
            "RGBLED_WIDGET_LAYER_COLOR": 6, "RGBLED_WIDGET_LAYER_DEBOUNCE_MS": 100,
        }
        config.update({f"RGBLED_WIDGET_LAYER_{i}_COLOR": i if i < 8 else 0 for i in range(32)})
        for name, (split, central, blinks, colors, usb, batteries) in VARIANTS.items():
            defines = dict(config, ZMK_SPLIT=split, ZMK_SPLIT_ROLE_CENTRAL=central,
                           ZMK_BLE=int(name not in ("usb-only", "quiet-pulse-usb", "quiet-pulse-ignore-aml-usb")), RGBLED_WIDGET_SHOW_LAYER_CHANGE=blinks,
                           RGBLED_WIDGET_SHOW_LAYER_COLORS=colors, RGBLED_WIDGET_CONN_SHOW_USB=usb,
                           RGBLED_WIDGET_BATTERY_SHOW_SELF=int(batteries == 0),
                           RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS=int(batteries == 1),
                           RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS=int(batteries == 2))
            if name.startswith("quiet-pulse-"):
                defines.update(RGBLED_WIDGET_SHOW_CONNECTION_STATUS=0,
                               RGBLED_WIDGET_SHOW_BATTERY_STATUS=0,
                               RGBLED_WIDGET_LAYER_SINGLE_PULSE=1,
                               RGBLED_WIDGET_INTERVAL_MS=0,
                               RGBLED_WIDGET_QUIET_PULSE_TEST=1)
            if name.startswith("quiet-pulse-ignore-aml"):
                defines.update(RGBLED_WIDGET_LAYER_IGNORE_MASK=0x200,
                               RGBLED_WIDGET_LAYER_BLINK_MS=1000,
                               RGBLED_WIDGET_LAYER_0_COLOR=4,
                               RGBLED_WIDGET_LAYER_9_COLOR=7)
            variants = [("sanitized", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-pie", "-no-pie"])]
            if mode == "full": variants.append(("optimized", ["-O2"]))
            for compiler_mode, options in variants:
                binary = work / (name + "-" + compiler_mode)
                command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wshadow",
                           "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-implicit-fallthrough",
                           *options, "-I" + str(work / "include"), "-I" + str(ROOT / "include"),
                           "-I" + str(ROOT / "tests/runtime"),
                           *("-D" + (key if key.startswith("ZMK_SPLIT_BLE_PERIPHERAL_COUNT") else "CONFIG_" + key) + "=" + str(value) for key, value in defines.items()),
                           str(ROOT / "tests/runtime/widget_harness.c"), "-o", str(binary)]
                subprocess.run(command, check=True)
                subprocess.run([str(binary)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
                print(name, compiler_mode, "PASS", flush=True)
            if mode == "full" and name == "quiet-pulse-central":
                # Keep the exact quiet fixture; enable each unwanted status
                # path independently and require its runtime assertion to fail.
                for option, diagnostic in (
                    ("SHOW_CONNECTION_STATUS", "indicate_connectivity_work.schedules"),
                    ("SHOW_BATTERY_STATUS", "initialized && !led_msgq.count"),
                ):
                    control = work / ("control-" + option.lower())
                    fault = list(command)
                    flag = "-DCONFIG_RGBLED_WIDGET_" + option + "=0"
                    fault[fault.index(flag)] = flag[:-1] + "1"
                    fault[-1] = str(control)
                    print(json.dumps({"control": option, "compile": fault}), flush=True)
                    subprocess.run(fault, check=True)
                    for repeat in range(1, 4):
                        result = subprocess.run([str(control)], capture_output=True, text=True)
                        print(json.dumps({"control": option, "repeat": repeat,
                                          "exit": result.returncode, "stderr": result.stderr}), flush=True)
                        assert result.returncode == -6 and diagnostic in result.stderr, option
                    print(option, "runtime control rejected 3/3 PASS", flush=True)
            if mode == "full" and name == "quiet-pulse-central":
                faults = (
                    ("claim-generation", "indicate_layer_pulse(generation);",
                     "ARG_UNUSED(generation);\n        indicate_layer_pulse(current_generation());",
                     "claimed layer work survived sleep"),
                    ("sleep-reschedule", "if (!led_sleeping && index != UINT8_MAX)",
                     "if (index != UINT8_MAX)", "sleeping layer change scheduled work"),
                    ("early-claim", "k_work_delayable_remaining_get(&layer_indicate_work) == 0",
                     "true", "new layer work ran before its debounce"),
                    ("duplicate-claim", "generation = layer_indicate_generation;\n        layer_indicate_pending = false;",
                     "generation = layer_indicate_generation;", "layer request emitted more than once"),
                )
                for label, original_text, replacement, diagnostic in faults:
                    shadow = work / label
                    shutil.copytree(ROOT / "src", shadow / "src")
                    source = shadow / "src/widget.c"
                    text = source.read_text()
                    assert text.count(original_text) == 1
                    source.write_text(text.replace(original_text, replacement))
                    harness = shadow / "tests/runtime/widget_harness.c"
                    harness.parent.mkdir(parents=True)
                    shutil.copy2(ROOT / "tests/runtime/widget_harness.c", harness)
                    control = work / ("control-" + label)
                    fault = [*command[:-3], str(harness), "-o", str(control)]
                    print(json.dumps({"control": label, "compile": fault}), flush=True)
                    subprocess.run(fault, check=True)
                    for repeat in range(1, 4):
                        result = subprocess.run([str(control)], capture_output=True, text=True)
                        print(json.dumps({"control": label, "repeat": repeat,
                                          "exit": result.returncode, "stderr": result.stderr}), flush=True)
                        assert result.returncode == -6 and diagnostic in result.stderr
                    print(label + " runtime control rejected 3/3 PASS", flush=True)
            if mode == "full" and name == "quiet-pulse-ignore-aml":
                shadow = work / "control-ignore-bypassed"
                shutil.copytree(ROOT / "src", shadow / "src")
                source = shadow / "src/widget.c"
                original = source.read_text()
                call = "pulse_layer_index(CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK)"
                assert original.count(call) == 4
                source.write_text(original.replace(call, "pulse_layer_index(0)"))
                harness = shadow / "tests/runtime/widget_harness.c"
                harness.parent.mkdir(parents=True)
                shutil.copy2(ROOT / "tests/runtime/widget_harness.c", harness)
                control = work / "control-ignore-bypassed.exe"
                fault = [*command[:-3], str(harness), "-o", str(control)]
                print(json.dumps({"control": "ignore-bypassed", "compile": fault}), flush=True)
                subprocess.run(fault, check=True)
                for repeat in range(1, 4):
                    result = subprocess.run([str(control)], capture_output=True, text=True)
                    print(json.dumps({"control": "ignore-bypassed", "repeat": repeat,
                                      "exit": result.returncode, "stderr": result.stderr}), flush=True)
                    assert result.returncode == -6 and "!layer_indicate_work.schedules" in result.stderr
                print("ignore-bypassed runtime control rejected 3/3 PASS", flush=True)

if __name__ == "__main__":
    main()
    subprocess.run(["python3", "-m", "unittest", "discover", "-s", str(ROOT / "tests"),
                    "-p", "test_timeout_width.py", "-v"], check=True)
