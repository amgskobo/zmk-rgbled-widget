# Owner tests

This repository owns its executable test logic and fixtures under `tests/`.

## Current test ownership and compatibility

Run the relevant public interface/build checks on both unchanged stock ZMK
and DYA when this owner supports both. See the selected core in the manifest.

Canonical entries (run from the repository root):

```bash
bash tests/run-host-docker.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

## Supported-core interface fixture

```bash
python3 tests/integration/run.py --workspace /absolute/resolved/workspace \
  --core stock-zmk --output /absolute/new/output
# Repeat with the separately resolved DYA workspace and --core dya.
```

The runner verifies clean pinned core/Zephyr identities, stages an unchanged
owner copy once, and builds only against already resolved dependencies. It
checks required Kconfig, actual compiled source units and the UF2. Outputs
include source/dependency identities, exact commands, config, DTS and binaries.
The fixture establishes API/DT compilation, not physical wiring or runtime
timing. No network update or dependency source patch is performed.
Both Python entries accept `--jobs` (default2) as the compiler budget. Keep
their fresh output outside source and account for other concurrent test jobs.

## Persistent layer color saturation

`tests/runtime/widget_harness.c:test_layers_and_boot` fills all 16 queue slots
with changing layer colors, drops the newest notification, then drains the queue.
The physical output requested from the controlled LED API must equal the latest
persistent target, with no timed blink or stale color remaining. This regression
fails against the previous worker, which replayed each old color payload. Both
sanitizer and optimized variants retain the earlier exact timed blink trace.
These controlled queue/LED checks do not establish real scheduling or GPIO timing.

## Caller, startup and sleep regressions

The canonical host entry runs all six original groups in seven feature/role configurations;
full repeats them under optimization as well as sanitizers. Original exact blink
edges, battery retry counts, queue saturation and threshold assertions remain.
Battery sampling/retry assertions now target the internal worker routine; public
behavior assertions require immediate bounded enqueue with no sampling or sleep.
The caller/sleep cases cover full queues, queued and running work, sleep/wake
during retries, changes while asleep and latest-color restoration. The controlled
mutex rejects nested acquisition and sleep under the output-state lock.

Three additional quiet-pulse configurations cover split central, USB-only and
peripheral roles in both sanitizer and optimized runs. They assert no boot,
connection or battery output, one exact timed color followed by off, activation
and release notifications, unchanged-layer suppression, obsolete queued layers,
and sleep/wake cancellation. Peripheral status remains off without layer output.
Full also enables each status path independently in the unchanged quiet fixture;
each compiled control must fail its runtime assertion three times.
Two additional masked configurations retain these assertions and exercise one
hundred automatic-layer entry/exit pairs without any scheduled pulse, a manual
layer below the automatic layer, return to base, queued output, an empty eligible
set and layer reordering. A compiled mask-bypass control must fail the unchanged
runtime fixture three times. All original configurations remain selected.

Run the native command below again with `--layer-pulse` and fresh evidence for
real-kernel quiet startup, one-shot colors and sleep/wake cancellation. This adds
a separate binary repeated three times; it preserves the original default/wide
cases and the default invalid-parent compiler guards.
Repeat with `--layer-pulse --ignore-aml` for real-kernel ignored-layer transitions
and a visible manual layer beneath it. Both supported-core interface entries
also retain their original BLE/USB and USB-only variants and add a masked case.
The masked native case uses a one-second duration. It retains the 300ms color
response bound after sleep cancellation and during a running pulse replacement,
then requires the new color to remain on at980ms and off by1020ms. A semaphore
retains a cancellation/update that arrives before the worker starts its wait;
serials discard older queued requests, including a return to the same layer.

The [retained focused entry](review-caller/README.md) runs the original four
assertions three times each. Its old three failure expectations intentionally
become product PASS after the repair; assertion bodies are unchanged.

```bash
python3 tests/native/run.py --zephyr /absolute/resolved/zephyr \
  --cmsis /absolute/resolved/cmsis --output /absolute/fresh/output
```

Resolve/read the selected dependencies first. This fixture uses the complete
production widget and behavior sources, real Zephyr threads, delayable work,
message queue, mutex, GPIO emulator and GPIO LED driver. It requests connectivity
before the LED thread starts, then checks queued-sleep, sleep/wake during an actual
blink, nonwaiting battery behavior and sleep/wake during actual retry sleeps.
One binary runs three times; process exit and the named completion are required.
The fixture keeps an unrelated GPIO LED device enabled and places the RGB
aliases under a second device. It checks the exact selected device and verifies
that the unrelated LED output remains unchanged throughout the lifecycle cases.
Two separate real-DTS builds must reject mixed parents and a parent without
`gpio-leds` compatibility using their specific compiler diagnostics.

Repeat the command with `--wide-indices` and another fresh output to place the
RGB children after 256 unrelated siblings. It runs all the same lifecycle
assertions three times, proving that LED API indices retain their uint32 width.
The default and wide runs fail against the previous arbitrary-device selection
and uint8 indices respectively; the before-fix binaries and ordered logs are
preserved externally to the owner source tree.
ZMK events, endpoint/battery inputs and behavior registration are controlled.
It is kernel/driver-API evidence, not physical GPIO, radio or power acceptance.

## 32-bit timeout arithmetic

Variable millisecond reservations use a private owner header and preserve the
full uint32 input range, exact ceiling and configured kernel tick ABI. Fractional
products and divisions use uint32; only the required tick result is widened.
Zero/forever handling, callback ownership, cancellation and output policy remain
at their existing call sites. This introduces no new limit on timer settings.

`test_timeout_width.py` runs in the canonical host/owner entry. It compares every
0..65535 interval, full-width boundaries and 65536 deterministic full-width
samples at 14 rates against an independent wide oracle, for both tick forms.
Full retains optimized, ASan/UBSan, measured nonzero complete header line/branch
coverage and m32 object/numeric execution. Four identified arithmetic faults
must produce three qualified assertion failures per tick form. A numerically
correct wide-divider control must fail the object-width guard. Existing output,
state, timing and supported-core assertions remain enabled. Kernel/physical
acceptance remains separate from this numerical and object-code check.
