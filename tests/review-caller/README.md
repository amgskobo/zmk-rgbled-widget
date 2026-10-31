# Retained caller and sleep regressions

Run `python3 tests/review-caller/run.py FRESH_OUTPUT` inside the pinned Linux
build image. The complete production sources use the existing runtime fixture's
controlled APIs. One sanitized binary is reused three times per case:

- Ready battery: positive control, no caller sleep.
- Zero local battery: the behavior returns without caller sleep or sampling.
- Zero local and two peripheral batteries: the caller still returns immediately;
  retries belong to the LED worker.
- A persistent layer-color item queued before sleep cannot relight the output
  when the worker drains it.

All four cases must pass, three times per binary. Before repair, the last three
failed their unchanged assertions: local-only wait 1000 controlled milliseconds,
all-zero wait 6000 milliseconds and output relighting after sleep. Those raw
failures and the original binary remain in the integrating review evidence.
These are controlled time/GPIO API observations, not real elapsed time or
electrical measurements. The default runtime suite additionally covers running
indications, retry/wake fencing, full queues and restoring the latest target.
