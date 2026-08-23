# 003: Delete hard-baked Axiom DSP code

- Depends on: 002
- Blocks: —

Remove Axiom-specific C++ registrations and `Program.cs` Axiom tab builders that duplicate the EEL chain. Keep only `LiveProg` path via `DspConfig.cpp:applyLiveprog` → `liveprogWrapper.c`.

Acceptance: `grep -rn BassBoost AxiomConsoleHarness` finds only config-compat, no effect code; CTest still green.
