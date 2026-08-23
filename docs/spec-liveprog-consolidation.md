# Spec: Consolidate Axiom Controller into LiveProg Controller

## Problem Statement

The Axiom-JamesDSP-Controller exists in two variants: a hard-baked Axiom DSP controller (BassBoost/Darwin/etc. in C++) and a generic LiveProg controller that loads any `.eel` script via EEL2. The Axiom binaural DSP (`axiom_binaural_dsp_v4.1.4.11.eel`) now loads correctly in the LiveProg variant (proven: `dragon_engine_signal` shows -12.00 dB trim, all 8 Dragon params audible, `LiveProgStringParser` result 1). Maintaining two controllers duplicates the autostart, config, and WASAPI pipeline, and every fix (e.g., `Program.cs:408` Shown-event autostart) must be ported twice.

## Solution

Keep a single controller binary: `AxiomJamesDSPController` (LiveProg). Ship the Axiom DSP as the bundled/default LiveProg script, not as baked C++. Existing Axiom installs migrate by setting `[LiveProg] file = <bundledEel>` and preserving `controller-state.json`. The hard-baked Axiom effect tabs are deprecated behind a flag, then deleted.

## User Stories

1. As a listener, I want to launch one controller and hear the Axiom binaural DSP by default, so I don't choose between variants.
2. As a listener, I want to load any `.eel` (Dragon, Axiom, custom) from the LiveProg tab, so I can switch DSP without reinstalling.
3. As a listener, I want my existing Axiom settings to survive the consolidation, so I don't reconfigure after update.
4. As a listener, I want Start with Windows / AutoStartProcessor to work in the consolidated build, so my DSP survives reboot (regression: `BeginInvoke` before handle).
5. As a listener, I want moving any LiveProg slider to audibly change sound within ~50 ms, so controls feel live (verified: 44 ms smoothing).
6. As a power user, I want the bundled Axiom EEL versioned in the repo, so I can diff DSP changes.
7. As a developer, I want one CTest suite (`liveprog_native`, `darwin_native`, `dragon_engine_signal`) to guard the pipeline, so I don't maintain two suites.
8. As a developer, I want the Axiom hard-baked C++ path removed, so fixes aren't duplicated.

## Implementation Decisions

- Modules modified: `AxiomJamesDSPController` (WinForms `Program.cs` AppPaths/config/UI), `AxiomJamesDSPCore` (static lib), `JamesDSPConsole` (`DspConfig.cpp`/`liveprogWrapper.c`).
- AppPaths already resolves `BundledEel` (`assets/Liveprog/axiom_binaural_dsp_v4.1.4.11.eel`); use it as `DataRoot/jamesdsp-controller.ini` default `[LiveProg] file` when no file is set.
- Config migration: on first run with consolidation, `CopyIfMissing` from legacy Axiom config; if `[LiveProg] file` empty and Axiom preset was active, set it to `BundledEel` and `enabled = true`.
- UI: LiveProg tab becomes primary; Axiom tab shows deprecation banner ("Load Axiom preset → LiveProg") and is hidden behind `AXIOM_LEGACY_UI` env flag, then removed.
- Seams for testing (highest possible, one seam): `JamesDSPCore.LiveProgStringParser` / `LiveProgSetVariable` / `DspController.applyLiveprog` — existing native tests already cover this; no new low-level seams.
- No API/schema change beyond `jamesdsp-controller.ini` `[LiveProg]` becoming canonical; `[BassBoost]`/`[Darwin]` etc. remain for backward compat but are ignored when LiveProg is enabled (or removed in final step).

## Testing Decisions

- Good tests assert external behavior (audible level/spectrum), not implementation. Prior art: `liveprog_native_tests.cpp` (lifecycle, transactional replacement, variable reruns `@slider`) and new `dragon_engine_test.cpp` (steady-state RMS after 24×2048 chunks, `trim -12.00 dB`, plus per-param probes).
- Coverage: `dragon_engine_signal` already sweeps all 8 Dragon params with tuned probes (1 kHz, 50 Hz, 10 kHz, silence); add an Axiom preset smoke: load `axiom_binaural_dsp_v4.1.4.11.eel`, `LiveProgSetVariable` on its declared params, assert audible delta.
- Keep `CTest` as the seam; no UI automation needed for this spec.

## Out of Scope

- New DSP features or EEL dialect changes
- Installer/packaging changes beyond bundling the `.eel`
- Android parity work (separate branch)

## Further Notes

- Current installed LiveProg build (commit `2ce7528`) is the baseline for consolidation; dev `DataRoot` is `AxiomConsoleHarness/` while installed is `%LOCALAPPDATA%\JamesDSP\Controller\`.
- Autostart fix is prerequisite and already shipped in this commit.
