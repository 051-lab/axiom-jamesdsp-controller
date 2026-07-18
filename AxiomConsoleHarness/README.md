# Axiom JamesDSP Windows Harness

Local-only experiment for controlling the native Windows JamesDSP console
processor with an Axiom-focused GUI.

## Run

Use:

```bat
run_axiom_controller.bat
```

The controller is single-instance. Launching it again activates the existing
window instead of creating another controller process.

Runtime paths are discovered from the executable location rather than a fixed
Windows user directory:

- Development builds walk upward to locate `AxiomConsoleHarness` and the
  repository build output.
- Packaged builds prefer processor and asset files bundled beside the
  controller.
- Installed builds store mutable config, runtime EEL files, profiles, and
  diagnostics under `%LOCALAPPDATA%\Axiom\JamesDSPController`.
- Development builds continue using the harness directory as their data root.
- Older absolute LiveProg paths in configs/profiles are remapped to the
  discovered runtime directory when possible.

Optional path overrides:

- `AXIOM_HARNESS_ROOT`
- `AXIOM_DATA_ROOT`
- `AXIOM_CONSOLE_EXE`
- `AXIOM_ACCEPTED_EEL`

## Package

Create the self-contained Windows package and zip with:

```bat
publish_axiom_app.bat -Zip
```

Output:

```text
dist\AxiomJamesDSPController-win-x64\
dist\AxiomJamesDSPController-win-x64.zip
```

The package includes the controller, native JamesDSP processor, accepted R011
EEL resource, route-test EEL files, clean default config, launcher, and package
readme. It does not include the external VB-CABLE driver.

Create the Windows installer with:

```bat
build_installer.bat
```

Release signing is intentionally separate from normal local builds. See
[`SIGNING.md`](SIGNING.md) and use `sign_release.ps1` only with a locally
installed code-signing certificate.

Output:

```text
dist\installer\AxiomJamesDSPController-0.2.0-win-x64-setup.exe
```

The installer targets 64-bit Windows, installs under Program Files, and can
create Desktop and sign-in shortcuts. Uninstall removes application files but
preserves profiles, settings, diagnostics, and runtime data under
`%LOCALAPPDATA%\Axiom\JamesDSPController`. Installing a newer build over an
existing installation therefore retains user data.

Fresh packaged launches open the Routing tab. `Setup & System` is the final tab
for onboarding and maintenance checks:

- required application files;
- VB-CABLE detection;
- recommended VB-CABLE-to-physical-output route;
- Listening profile availability.

The recommended setup action selects stereo `CABLE Input`, prefers Realtek or
EarPods as the physical output, restores Axiom LiveProg, and selects the
resilient `200 ms` buffer. The native queue targets one-fifth fill, so the extra
capacity primarily protects against Windows scheduling stalls while steady
queued latency remains near `40 ms`. The Setup tab can also create a Desktop
shortcut.

The GUI can start and stop the processor. It runs:

```bat
AxiomJamesDSPConsole.exe --watch-config -c axiom-liveprog-test.ini -i <capture-index> -o <output-index>
```

The processor captures a selected Windows render endpoint with WASAPI loopback
and outputs to a different selected render endpoint. Route the player/browser
into the capture/source endpoint, then listen through the processed output
endpoint. If both endpoints are the same physical output, the unprocessed audio
will bypass the processor.

## Files

- `axiom-liveprog-test.ini`: generated config watched by the processor.
- `runtime/axiom-liveprog-current.eel`: generated runtime copy of the accepted
  Axiom LiveProg script with current slider values.
- `AxiomJamesDSPController/`: .NET WinForms controller.

The accepted Axiom EEL in the main repository is not edited by this harness.

## Native Windows Direction

This harness is the Phase 1 base for a native Windows Axiom/JamesDSP app.

Current target route:

```text
Windows apps
  -> source playback endpoint, preferably VB-CABLE for v1
  -> AxiomJamesDSPConsole WASAPI loopback capture
  -> JamesDSP/Axiom processing
  -> processed output endpoint, such as Speaker (Realtek(R) Audio) or EarPods
```

V1 route target:

```text
Windows default output: VB-CABLE input/source endpoint
Axiom capture/source: VB-CABLE input/source endpoint
Axiom processed output: real listening device
```

The controller now includes a `Use VB-CABLE -> Output` route preset. It tries
to select a VB-Audio cable endpoint as the capture/source while preserving the
currently selected real output device, then requests that Windows use the
selected source as the default playback endpoint.

Known-good headphone route, confirmed 2026-06-17:

```text
Windows default output: CABLE Input (VB-Audio Virtual Cable)
Axiom capture/source: CABLE Input (VB-Audio Virtual Cable)
Axiom processed output: Headset (EarPods)
LiveProg: Axiom runtime EEL restored
Buffer: 30 ms playback-confirmed; 60 ms remains the conservative fallback
```

Both the low-cut and pulse-gate LiveProg tests were audibly confirmed on this
route. The controller includes `Use VB-CABLE -> Output` for returning to the
preferred route after device changes, while the Steam presets remain fallback
options.

Routing upgrade note:

- `Steam Streaming Speakers` is acceptable for current prototyping because it
  is already present and behaves as a virtual playback endpoint.
- It is not the preferred long-term Axiom source endpoint because it belongs to
  Steam Remote Play rather than to this app.
- Preferred next routing upgrade: install and validate a dedicated virtual
  audio cable such as VB-Audio Virtual Cable, then use that cable as the
  Axiom capture/source endpoint.
- ASIO4ALL is familiar from DAW workflows and may be useful for later
  low-latency music-production/plugin-host experiments, but it is not the
  immediate best fit for system-wide Windows app audio capture because the
  current harness is built around WASAPI loopback and Windows playback
  endpoints.
- Keep this decision open for the native Windows product path: WASAPI virtual
  cable first, ASIO/plugin-host path later if Axiom grows a DAW-facing mode.

Phase 1 hardening:

- The controller enforces one GUI instance and activates the existing window
  on repeated launch.
- Application resources and mutable data use discovered/relative paths instead
  of machine-specific `C:\Users\...` paths.
- A self-contained `win-x64` package builder assembles the controller, native
  processor, accepted EEL resource, test scripts, defaults, and launcher.
- Fresh installs use a Setup tab with readiness checks, recommended route
  configuration, audible test loading, profile creation, and Desktop shortcut
  creation.
- Operational tabs are ordered `Routing`, `Axiom`, `Profiles`, `Core Effects`,
  `EQ + Dynamics`, `Files`, `Diagnostics`, and `Setup & System`; Routing always
  opens first.
- Unexpected processor exits trigger at most three automatic retries within a
  60-second window, delayed by 1.5 seconds. Expected stops, route disconnects,
  and application exit do not consume retries. Repeated failure requires a
  manual Start Processor action, which resets the retry window.
- The controller owns the processor lifecycle.
- Starting the processor stops stale console instances first.
- The controller surfaces duplicate processor conflicts.
- The controller can set the Windows default playback endpoint to the selected
  capture/source endpoint.
- The controller blocks starts when the capture/source and processed output are
  the same endpoint.
- The controller warns when the selected source is not a recognized virtual
  playback endpoint.
- The controller validates the selected LiveProg file before start.
- The controller has route presets for VB-CABLE, Steam-to-Realtek, and
  Steam-to-EarPods.
- Saved routes and profiles use stable Windows endpoint IDs, with numerical
  indices retained only as backward-compatible fallback values.
- The VB-CABLE preset prefers the normal stereo `CABLE Input` endpoint instead
  of the optional `CABLE In 16ch` endpoint.
- The controller checks the saved route every three seconds. If the source or
  output disconnects, it preserves the intended endpoint IDs, stops an active
  processor, shows a recovery-waiting state, and automatically restores the
  route and restarts processing when both endpoints return.
- `Follow listening device` is enabled by default. When VB-CABLE is available,
  the controller keeps it as the source endpoint, prefers newly connected
  listening devices such as EarPods, headphones, USB, or Bluetooth outputs, and
  falls back to Realtek or another physical output when the external device is
  removed.
- The Routing and Diagnostics tabs show explicit route states: Ready,
  Processing, Waiting for device, Recovering, Recovered, and Needs action.
  They also show the last route event so device changes and recovery actions
  are visible without reading raw logs.
- The controller includes a one-click Steam-to-Realtek route preset for laptop
  speaker testing.
- Temporary route-test EEL files live under `runtime/` and are not accepted
  Axiom candidates.
- Runtime test scripts must include explicit `// UI Defaults` assignments in
  `@init`; without that block, JamesDSP may load the script without applying
  the intended audible defaults.

Phase 2 audio-engine hardening:

- Capture audio is converted from the Windows endpoint format to normalized
  interleaved float before JamesDSP processing.
- Processed float audio is converted back to the selected render endpoint
  format instead of casting raw WASAPI buffers.
- Supported endpoint sample formats currently include float32, int16, packed
  int24, and int32 PCM-style formats.
- The console logs capture/render format details and periodic audio health
  counters for processed frames, dropped frames, silent frames, packets, and
  conversion errors.
- The console also reports capture discontinuities, render-buffer starvation,
  render API errors, average/maximum DSP-call time, and observed render-padding
  range. The controller shows these measurements in the Audio Health panel and
  includes them in exported diagnostic reports.
- Basic channel adaptation copies matching channels, duplicates mono to stereo,
  and silences unmatched output channels.
- LiveProg parser success now follows the JDSP contract: positive return codes
  load and enable the script, while `0` or negative return codes are treated as
  compile/load failures.
- The controller surfaces console health telemetry on the Routing tab, including
  capture/render formats, processed frames, dropped frames, silent frames,
  packets, and conversion errors.
- The controller exposes latency modes backed by the console `--buffer-ms`
  option: low latency `30 ms`, balanced `60 ms`, safe playback `100 ms`, extra
  safe `150 ms`, and resilient `200 ms`.
- The controller warns when the processor is running but no audio packets are
  arriving at the capture/source endpoint.
- The controller can save a listening profile under `profiles/`, restore that
  profile, and load a qualification baseline that restores the Axiom runtime
  EEL, disables Crossfeed, sets post gain to `0 dB`, and uses the `200 ms`
  resilient buffer.
- The Profiles tab manages multiple named listening profiles. It can create,
  update, load, duplicate, rename, delete, import, and export profiles. User
  profile filenames use stable generated IDs, so display-name changes do not
  break references.
- The qualification profile is protected from overwrite, rename, and deletion.
  It remains a fixed baseline action rather than a user-editable listening
  preset.
- Loading a profile refreshes the visible controls as well as the generated
  config file, so the UI should match the active route/profile state.
- Profile schema v2 stores the complete JamesDSP configuration, stable route
  endpoint IDs, buffer selection, and all Axiom slider values. Older partial
  profile files remain loadable.
- Profile writes are atomic. The qualification profile resets Axiom sliders to
  accepted R011 defaults and disables optional host effects, while a listening
  profile preserves the user's complete listening state.
- On startup, the controller reports whether VB-CABLE is visible; when it is
  missing, Steam Streaming Speakers remains the working fallback route.
- The controller has a Diagnostics tab and can export diagnostic reports under
  `diagnostics/` with route, processor, script, profile, audio-health, and log
  context.
- Every processor telemetry interval is appended to
  `diagnostics/health-history.jsonl`. The controller warns on newly increased
  dropped frames, conversion errors, capture discontinuities, render
  starvations, render errors, DSP packet-deadline misses, or full-buffer
  critical stalls. Graceful shutdown also writes a session summary.
- Telemetry distinguishes packet-deadline misses from critical stalls that
  exceed the configured WASAPI buffer budget. This avoids treating an isolated
  Windows scheduling outlier as equivalent to dropped or starved audio.
- The Diagnostics tab can open or clear persistent health history. Exported
  reports include the session warning count and health-history path. A compact
  diagnostic summary can also be copied directly to the clipboard.
- Before setting the selected capture endpoint as the Windows default, the
  controller records the previous default endpoint by stable ID. It can restore
  that endpoint on demand or automatically on application exit. If another
  application changes the default after Axiom takes ownership, Axiom stops
  claiming ownership rather than restoring stale state.
- Optional lifecycle settings support Start with Windows, automatic processor
  startup when the saved route is valid, close/minimize to the notification
  area, and previous-output restoration on exit. The tray menu exposes Open,
  Start, Stop, and Exit commands.
- Runtime verification scripts:
  - `runtime/axiom-test-lowcut.eel`: aggressive static high-pass test.
  - `runtime/axiom-test-pulse-gate.eel`: obvious on/off level pulse for
    confirming LiveProg is audibly in the path.
- Loading either confidence-test script starts the processor automatically when
  the selected route and script are valid. `Restore Axiom + Start` returns to
  the generated Axiom LiveProg script and starts processing from the current
  route.

## Automated Verification

Run the Windows smoke suite with:

```bat
tests\run_smoke_tests.bat
```

Use `-SkipBuild` to verify existing artifacts. The suite checks package
contents, EEL test defaults, installer presence, endpoint discovery, stable
default-device reporting, isolated first-run state, route selection,
single-instance behavior, visible Routing controls, processor startup,
bounded crash recovery, persistent health history, and profile JSON validity.

For unattended playback, live-parameter, and recovery testing, see
[`SOAK-TESTING.md`](SOAK-TESTING.md). The soak harness supports both the
portable package and an installed Program Files application root. It generates
local JSON and Markdown evidence and restores the preceding Windows default
endpoint when finished.

For the current controller wrap-up checklist, see
[`CONTROLLER-WRAPUP.md`](CONTROLLER-WRAPUP.md). Use
`Start 1-Hour Validation Test.bat` as the shorter installed-build validation
gate before scheduling the full eight-hour qualification run.

The Android-parity native and metadata foundation, pinned references, audit,
and non-routing test commands are recorded in
[`ANDROID-PARITY-FOUNDATION.md`](ANDROID-PARITY-FOUNDATION.md).

Live Axiom slider changes are sent to JamesDSP through the validated
`LiveProgSetVariable` API.
Changing a slider no longer recompiles the EEL script. Script compilation is
reserved for processor startup or selecting a different LiveProg file, which
keeps normal control changes out of the real-time audio path.

## Remaining Product Work

1. Pass the full overnight installed-build soak gate, followed by the manual
   endpoint-disconnect and sleep/wake recovery pass.
2. Add cryptographic signing before distributing outside the development
   machine. Controller and installer release metadata are centralized at
   version `0.2.0`; `signtool.exe` is installed, but a trusted code-signing
   certificate still needs to be provisioned.
3. Keep ASIO4ALL/native-ASIO support as a future engine track. FL Studio with
   ASIO4ALL did not feed either visible VB-CABLE render endpoint during the
   2026-06-29 routing test, so the current Controller should continue to target
   WASAPI/VB-CABLE routes.
4. Revisit a custom virtual endpoint only if dedicated VB-CABLE routing proves
   insufficient.
