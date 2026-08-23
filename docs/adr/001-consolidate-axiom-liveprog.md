# ADR 001: Consolidate Axiom Controller into LiveProg Controller

- Date: 2026-08-21
- Status: Accepted
- Deciders: 051-lab

## Context

Two Windows controllers exist: an Axiom-baked DSP build and a generic LiveProg EEL2 loader. The Axiom EEL (xiom_binaural_dsp_v4.1.4.11.eel) now loads and responds correctly in the LiveProg path (native LiveProgStringParser + LiveProgSetVariable with 44 ms smoothing, verified by dragon_engine_signal).

## Decision

Keep one controller: LiveProg. Ship the Axiom DSP as the bundled/default .eel asset. Deprecate, then delete, the hard-baked Axiom C++ effect chain.

## Consequences

- One pipeline to maintain, one CTest suite, one DataRoot config.
- DSP iteration becomes .eel edits, no C++ rebuild.
- Migration: empty [LiveProg] file → BundledEel on first run.
- Risk: users with legacy [BassBoost]/[Darwin] configs need migration notice.

