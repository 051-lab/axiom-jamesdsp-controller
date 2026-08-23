# 004: Axiom preset smoke test

- Depends on: 001
- Blocks: —

Add native test `axiom_engine_smoke` mirroring `dragon_engine_test.cpp`: load `axiom_binaural_dsp_v4.1.4.11.eel`, assert `LiveProgStringParser == 1`, `LiveProgSetVariable` on its declared params, audible delta (tuned probes). Register in CTest.

Acceptance: `ctest -R axiom` passes; guards the bundled preset.
