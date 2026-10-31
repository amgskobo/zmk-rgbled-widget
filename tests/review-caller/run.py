#!/usr/bin/env python3
"""Retained caller/sleep regressions, reusing one sanitized binary per run."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=False)
headers = set()
for source in (root/'src/widget.c', root/'src/behaviors/behavior_rgbled_widget.c'):
    headers.update(re.findall(r'#include <([^>]+)>', source.read_text()))
headers.remove('zmk_rgbled_widget/widget.h')
for name in headers:
    path=out/'include'/name; path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "stubs.h"\n')
config = dict(RGBLED_WIDGET=1, ZMK_USB=1, ZMK_BATTERY_REPORTING=1, ZMK_SPLIT=1,
 ZMK_SPLIT_ROLE_CENTRAL=1, ZMK_BLE=1, ZMK_SPLIT_BLE=1, ZMK_BEHAVIOR_METADATA=0,
 RGBLED_WIDGET_INTERVAL_MS=500, RGBLED_WIDGET_CONN_BLINK_MS=1000,
 RGBLED_WIDGET_CONN_COLOR_USB=6, RGBLED_WIDGET_CONN_COLOR_CONNECTED=4,
 RGBLED_WIDGET_CONN_COLOR_ADVERTISING=3, RGBLED_WIDGET_CONN_COLOR_DISCONNECTED=1,
 RGBLED_WIDGET_BATTERY_BLINK_MS=2000, RGBLED_WIDGET_BATTERY_LEVEL_HIGH=80,
 RGBLED_WIDGET_BATTERY_LEVEL_LOW=20, RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL=5,
 RGBLED_WIDGET_BATTERY_COLOR_HIGH=2, RGBLED_WIDGET_BATTERY_COLOR_MEDIUM=3,
 RGBLED_WIDGET_BATTERY_COLOR_LOW=1, RGBLED_WIDGET_BATTERY_COLOR_CRITICAL=1,
 RGBLED_WIDGET_BATTERY_COLOR_MISSING=5, RGBLED_WIDGET_LAYER_BLINK_MS=100,
 RGBLED_WIDGET_LAYER_COLOR=6, RGBLED_WIDGET_LAYER_DEBOUNCE_MS=100,
 RGBLED_WIDGET_SHOW_LAYER_CHANGE=0, RGBLED_WIDGET_SHOW_LAYER_COLORS=1,
 RGBLED_WIDGET_CONN_SHOW_USB=1, RGBLED_WIDGET_BATTERY_SHOW_SELF=0,
 RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS=1, RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS=0)
config.update({f'RGBLED_WIDGET_LAYER_{i}_COLOR':i if i<8 else 0 for i in range(32)})
compiler=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wshadow',
 '-Wno-unused-function','-Wno-unused-parameter','-Wno-implicit-fallthrough',
 '-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
 '-I'+str(out/'include'),'-I'+str(root/'include'),'-I'+str(root/'tests/runtime'),
 '-DZMK_SPLIT_BLE_PERIPHERAL_COUNT=2',
 *('-DCONFIG_'+k+'='+str(v) for k,v in config.items()),
 str(root/'tests/review-caller/caller.c'),'-o',str(out/'caller')]
(out/'compile-command.json').write_text(json.dumps(compiler,indent=2))
with (out/'compile.log').open('w') as log: subprocess.run(compiler,check=True,stdout=log,stderr=subprocess.STDOUT)
(out/'binary.sha256').write_text(hashlib.sha256((out/'caller').read_bytes()).hexdigest())
rows=[]
for case in ('ready','local-zero','all-zero','sleep-queued'):
 for repeat in range(1,4):
    result=subprocess.run([str(out/'caller'),case],capture_output=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
    (out/f'{case}-{repeat}.stdout').write_bytes(result.stdout)
    (out/f'{case}-{repeat}.stderr').write_bytes(result.stderr)
    rows.append(dict(case=case,repeat=repeat,exit=result.returncode))
    assert result.returncode==0, (case, repeat, result.stderr.decode(errors='replace'))
(out/'results.json').write_text(json.dumps(rows,indent=2))
print('Ready, local-zero, all-zero, sleep-queued PASS 3/3 each')
