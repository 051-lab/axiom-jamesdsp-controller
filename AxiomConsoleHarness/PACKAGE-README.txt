JamesDSP Controller

Run "JamesDSPController.exe" or "Launch JamesDSP Controller.cmd".

First run:
1. Install VB-CABLE from https://vb-audio.com/Cable/ and reboot Windows.
2. Open Routing and choose the physical device you will hear.
3. Select "Use VB-CABLE -> Output".
4. Start the processor and confirm audio reaches the physical output.
5. In Files, select any compatible EEL2 script as the LiveProg source.
6. Enable LiveProg; its declared parameters appear in the LiveProg tab.
7. Save a named listening profile if desired.

LiveProg:
- The selected EEL source is never modified.
- Script parameter values are remembered independently for each EEL file.
- @sample is required; @init, @slider, and @block are supported and optional.
- "Load Bundled Axiom" remains available as an example/compatibility script.

Darwin:
- Select a Darwin .zip or .darwin filter package in the Darwin tab.
- Choose a filter, optional harmonic amount, and automatic headroom.
- An invalid replacement leaves the last working filter active.

Mutable settings, profiles, runtime files, and diagnostics are stored under:
%LOCALAPPDATA%\JamesDSP\Controller

Existing data under %LOCALAPPDATA%\Axiom\JamesDSPController is migrated on
first use. Uninstall preserves Local AppData. VB-CABLE is an external Windows
driver and is not redistributed with JamesDSP Controller.
