# JamesDSP Controller Windows Soak Testing

The soak harness validates the native Windows controller and processor without
using private music. It creates a quiet deterministic 220 Hz probe, routes it
through the selected VB-CABLE source, exercises configuration hot reloads and
bounded crash recovery, then restores the preceding Windows default endpoint.

All test state and evidence remains local under:

```text
%LOCALAPPDATA%\JamesDSP\SoakTests\<timestamp>\
```

The harness uses an isolated controller data root. It does not overwrite the
normal listening profiles, settings, runtime EEL, or health history.

## Quick Validation

Run a five-minute development check:

```bat
tests\run_soak_test.bat -DurationMinutes 5 -CrashCount 2 -ConfigReloadCount 4 -SkipBuild
```

This catches orchestration, parameter-reload, crash-recovery, and telemetry
regressions before a longer run.

## One-Hour Gate

The simplest way to start the installed one-hour validation is to double-click:

```text
Start 1-Hour Validation Test.bat
```

Run against a freshly built portable package:

```bat
tests\run_soak_test.bat -DurationMinutes 60 -CrashCount 0 -ConfigReloadCount 12
```

Run against the installed application:

```powershell
tests\run_soak_test.ps1 `
  -ApplicationRoot "$env:ProgramFiles\JamesDSP Controller" `
  -DurationMinutes 60 `
  -CrashCount 0 `
  -ConfigReloadCount 12 `
  -SkipBuild
```

Keep crash recovery as a separate focused gate:

```powershell
tests\run_soak_test.ps1 `
  -ApplicationRoot "$env:ProgramFiles\JamesDSP Controller" `
  -DurationMinutes 12 `
  -CrashCount 3 `
  -ConfigReloadCount 3 `
  -SkipBuild
```

Separating these gates prevents repeated endpoint teardown from obscuring
continuous-playback endurance while retaining explicit crash-recovery evidence.

## Overnight Gate

The simplest way to start the full run is to double-click:

```text
Start 8-Hour Qualification Test.bat
```

That launcher uses the saved VB-CABLE to EarPods qualification route, runs the
strict preflight, temporarily disables AC sleep, starts the 8-hour installed
gate, and restores the previous AC sleep timeout when it exits.

Use the installed application for the release-candidate soak:

```powershell
tests\run_soak_test.ps1 `
  -ApplicationRoot "$env:ProgramFiles\JamesDSP Controller" `
  -DurationMinutes 480 `
  -CrashCount 0 `
  -ConfigReloadCount 24 `
  -SkipBuild
```

Keep the machine awake and connected to AC power. Disable Windows Update
restarts for the test window. Prefer `tests\run_overnight_gate.bat`; it verifies
the release prerequisites, temporarily disables AC sleep, runs the installed
gate, and restores the prior AC sleep timeout in all exit paths.

The overnight wrapper runs release preflight with strict quiet-host checking.
The preflight blocks clear setup failures such as pending reboot state,
competing JamesDSP Controller processors, route loss, package hash mismatch, or active
system churn during the observation window. Non-strict preflight reports the
same Hyper-V, HP recovery, Bluetooth, Windows Error Reporting, display,
Windows Update, and power events as warnings so the operator can choose a
better window. The quiet probe is intentionally audible so the test exercises
the full render path; use `-SkipAudioProbe` only for control-plane testing.

## Automated Gates

A run passes only when:

- the controller and processor remain alive;
- persistent health telemetry contains multiple samples;
- frames and packets are processed;
- every planned processor crash recovers;
- every configuration hot reload preserves the processor PID;
- dropped frames, conversion errors, render errors, and render starvations
  remain zero;
- capture discontinuities do not produce dropped frames or render starvation;
- no DSP call exceeds the configured audio buffer budget;
- DSP packet-deadline misses remain at or below `10%` of calls;
- maximum render padding remains at or below `90%` of the WASAPI buffer.

Power-source changes are reported separately as an environment warning. A run
can pass all audio-integrity gates while remaining unsuitable as clean release
evidence if Windows switches between AC and battery during the measurement.

The report retains the absolute maximum DSP-call time as diagnostic context.
Windows can occasionally preempt the processing thread beyond one packet
deadline while the larger WASAPI buffer absorbs it. That is recorded as a
deadline miss; it is not treated as an audible failure unless the miss rate
shows sustained pressure above the gate, render headroom is exhausted, a call
exceeds the full buffer budget, or audio-health counters show dropped/starved
output.

Each run writes `soak-report.json`, `soak-report.md`, the isolated
`health-history.jsonl`, and the controller session summary. Reports include
classification, partial metrics, health-analysis summaries, and environment
event windows even when the harness exits through an error path. A failed or
partial gate is evidence to investigate, not a reason to loosen the threshold
without tracing the event.

For post-run health analysis, run:

```powershell
tests\analyze_soak_health.ps1 `
  -RunRoot "$env:LOCALAPPDATA\JamesDSP\SoakTests\<timestamp>" `
  > "$env:LOCALAPPDATA\JamesDSP\SoakTests\<timestamp>\health-analysis.json"
```

The analyzer summarizes drop, discontinuity, deadline-miss, and reload timing
without replaying the soak or touching the audio route.

## Manual Device-Recovery Pass

Physical endpoint loss cannot be simulated safely by the unattended harness.
After the one-hour automated gate:

1. Start normal playback through JamesDSP Controller.
2. Disconnect the selected USB or wired output.
3. Confirm that the processor stops and the controller reports route recovery
   waiting.
4. Reconnect the same endpoint.
5. Confirm that its stable endpoint ID is restored and processing restarts.
6. Repeat once across Windows sleep and wake.
7. Export Diagnostics and retain the local report with the soak evidence.

The manual pass fails if unprocessed audio bypasses JamesDSP Controller, the wrong physical
output is selected, processing does not resume, or health errors continue
increasing after recovery.
