# 002: Deprecate Axiom tab behind flag

- Depends on: 001
- Blocks: 003

Hide Axiom hard-baked tab behind `AXIOM_LEGACY_UI` env flag; when hidden, show banner in LiveProg tab: "Axiom preset moved — Load Axiom preset → LiveProg". No deletion yet.

Acceptance: default launch shows only LiveProg tab; with flag, old tab still works.
