# 001: Default LiveProg to bundled Axiom EEL

- Depends on: —
- Blocks: 002

Make `AppPaths.BundledEel` the default `[LiveProg] file` when empty. On `LoadConfig`/`RunFirstRunChecks`, if `[LiveProg] file` is empty and no legacy preset is active, set `file = BundledEel` and `enabled = true`, then `SaveConfiguration`. No UI change.

Acceptance: fresh install (no ini) launches with Axiom DSP audible; `DataRoot/jamesdsp-controller.ini` contains bundled path.
