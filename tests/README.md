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
Each supported core retains BLE+USB, USB-only and masked quiet-pulse builds,
and also builds status-only, hidden-USB-color and split-peripheral status
variants. `tests/integration/dependencies.json` selects stock ZMK
`5b51501fead672c41b5cfb396f3dafe0894bf4e9`, DYA
`e5c9b6915b56801193e359dd9bad4a167ce0d1b8` and Zephyr
`10ba6d0cb38bc3d258775d27982f707599320085`. Resolve each clean workspace in
container storage before running; ordinary test execution does not fetch inputs.

Both exact supported-core pins already provide `app/boards/shields/tester_xiao/`:
the real shield has eleven direct-scan inputs and eleven mapped positions. The
owner's unchanged eleven bindings use that shield together with `rgbled_adapter`;
no sibling test fixture, duplicate shield or dependency patch is required.
`tests/integration/config/tester_xiao.conf` explicitly enables BLE, USB and settings
for the base matrix, overriding the tester shield's BLE/settings defaults.
At the pinned Zephyr revision, the owner shield configuration is merged before
`EXTRA_CONF_FILE`; USB-only still disables BLE and the split-peripheral fragment
still disables USB. All six variants per core retain their configuration/source/
UF2 assertions and additionally require settings to remain enabled. These are
build-only fixtures and do not establish wiring or physical transport acceptance.
The split-peripheral USB-disabled check accepts `n` or an omitted USB symbol,
as produced when the pinned core's role dependency is unmet, and rejects any
enabled USB assignment. The split-role and every positive configuration check
remain required.

## Persistent layer color saturation

`tests/runtime/widget_harness.c:test_layers_and_boot` fills all 16 queue slots
with changing layer colors, drops the newest notification, then drains the queue.
The physical output requested from the controlled LED API must equal the latest
persistent target, with no timed blink or stale color remaining. This regression
fails against the previous worker, which replayed each old color payload. Both
sanitizer and optimized variants retain the earlier exact timed blink trace.
These controlled queue/LED checks do not establish real scheduling or GPIO timing.

## GPIO driver failure and recovery

Every host feature/role configuration additionally checks an unready device,
independent channel failures during on and off, retrying the same target after
recovery, and a failed sleep-off followed by a fresh sleep request. The fixture
requires no driver calls before readiness and preserves successful channels
while retrying failed channels. All earlier exact GPIO edge/time assertions
remain. Full adds `unready-driver` and `failed-channel-cache` compiled controls;
each must fail its named assertion three times. The original source fails the
unready-device assertion. These controlled API failures establish bookkeeping
and retry behavior, not electrical fail-off or real driver recovery.

## Caller, startup and sleep regressions

Every host configuration checks a queue-item budget of 16 bytes without
single-pulse layer output and 20 bytes with it enabled. The queue still retains
all 16 requests and every existing saturation assertion. The original source
fails the non-pulse storage assertion because it reserves pulse identity fields
for disabled consumers; one sanitized original binary is rejected by that named
runtime assertion three times. Actual linked ARM queue symbols establish the
production saving separately from this host ABI check.

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

Debounced sleep cases also retain a change reserved before sleep, a change made
while sleeping and a new post-wake change while old work is pending. The host
fixture additionally enters sleep between callback claim and queue admission,
and checks that an old callback cannot consume a newer timer early or twice.
Full compiles four independent faults in these boundaries and requires each to
fail its named runtime assertion three times. Both native pulse variants retain
all earlier lifecycle assertions and run the new real-workqueue cases three times.

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

The native entry accepts `--profile default|optimized|asan|asan-ubsan`. Repeat
all seven feature/role variants with fresh output directories for each profile.
`optimized` selects Zephyr speed optimization; sanitizer profiles explicitly set
`ASAN_RECOVER=n`, add `-fno-sanitize-recover=all`, and run with halt-on-error.
Inspect the effective config and complete compiler command when classifying the
result. Parent-DTS compiler controls run in the default base variant only; the
summary reports the actual number of guards checked. Each profile still builds
once and requires three completed runs of that exact binary.

`native/verifier.py` requires the selected nonempty completion markers, exit0,
and no sanitizer diagnostic on either stream. Pulse runs additionally require
`RGB_DEBOUNCED_SLEEP_COMPLETE`; status runs retain both connectivity/color
markers. A recoverable finding rejects the clean gate even if execution reaches
all markers and exits0. `test_native_verifier.py` retains positive/negative
marker, exit, stream and benign-setting cases, plus actual recovering ASan and
UBSan processes. Their exit0, diagnostic and completion prerequisites are checked
outside the verifier. Full runs `native/verifier_negative_control.py`: the
original exit/marker-only predicate must fail both live tests specifically with
`ValueError not raised`. Its two expected assertion failures are control
evidence, separate from product and validator PASS counts.
The original predicate must also pass the completion and benign-setting positive
controls; setup failures or changed positive behavior do not qualify.

The supported-core integration entry retains one exact staged owner snapshot,
compiled-source hashes, generated source bytes and generated headers before
temporary build cleanup, including on build failure. Config, DTS, command list,
ELF and UF2 remain part of every variant's evidence.

## Status sleep and deferred connectivity

All seven original host feature/role configurations now exercise sleep with
queued, full-queue and running status indications, including a battery retry.
ACTIVE and IDLE leave status-only/peripheral output off; persistent colors
restore their latest target. Sleeping critical-battery and connectivity requests
are suppressed. Existing exact edge/time/retry assertions are retained.

Controlled callback interleavings cover a connectivity reservation pending at
sleep, sleep between claim and admission, an old callback dispatched before a
new post-wake debounce, and duplicate dispatch. The original endpoint/preference
table is expanded with independent BLE connected/open state; selecting USB with
its indicator disabled follows the BLE profile state. Peripheral connection
colors remain independent of the central endpoint.

The full host entry adds independent status-only sleep, connectivity retirement,
claim-generation, early-claim, duplicate-claim and USB-hidden color faults. Every
compiled control must fail its designated behavioral assertion three times;
compile/dependency failures do not establish detection. All original quiet-status,
mask, layer-debounce and arithmetic negative controls remain enabled.

Run the real-kernel entry three more times with separate fresh output directories:

```bash
python3 tests/native/run.py --zephyr "$RESOLVED_ZEPHYR" --cmsis "$RESOLVED_CMSIS" \
  --output "$FRESH_STATUS_OUTPUT" --status-only
python3 tests/native/run.py --zephyr "$RESOLVED_ZEPHYR" --cmsis "$RESOLVED_CMSIS" \
  --output "$FRESH_PERIPHERAL_OUTPUT" --status-only --peripheral
python3 tests/native/run.py --zephyr "$RESOLVED_ZEPHYR" --cmsis "$RESOLVED_CMSIS" \
  --output "$FRESH_USB_HIDDEN_OUTPUT" --hide-usb
```

Each entry builds once and runs that binary three times, preserving the four
existing native variants. Status-only and peripheral cases use actual threaded
blink/retry/queue sleep cancellation. Every non-pulse native variant blocks the
real system workqueue while scheduling connectivity across sleep/wake, requires
quiet restored output, then verifies a fresh post-wake request. It also checks
actual LED/GPIO-emulator colors for the selected USB endpoint with connected,
open and disconnected BLE profiles, or the peripheral's two connection states.
`RGB_CONNECTIVITY_SLEEP_COMPLETE` and `RGB_ENDPOINT_COLORS_COMPLETE` are required
along with the original completion marker in every non-pulse repeat. These are
controlled inputs to actual kernel/driver APIs, with no physical power claim.

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
