# Axiom Controller Wrap-Up Checklist

> Historical Axiom qualification checklist. For the current generic LiveProg
> controller, Darwin module, package names, and validation commands, use
> [README.md](README.md) and [SOAK-TESTING.md](SOAK-TESTING.md).

This checklist defines the remaining work before shifting attention back to
the Axiom-DSP EEL track.

## Current Status

- Application name, icon, installer, and GitHub fork are established as
  Axiom JamesDSP Controller.
- The preferred route is VB-CABLE source to a physical listening output.
- Known-good current route:

```text
Windows default output: CABLE Input (VB-Audio Virtual Cable)
Axiom capture/source: CABLE Input (VB-Audio Virtual Cable)
Axiom processed output: Headset (EarPods)
Buffer: 60 ms conservative listening route
```

- The controller supports automatic listening-device following, route recovery,
  lifecycle options, profiles, health telemetry, diagnostic export, and
  confidence-test EEL scripts.
- ASIO4ALL was tested with FL Studio on 2026-06-29. ASIO4ALL did not deliver
  audio into either VB-CABLE render endpoint visible to the current WASAPI
  loopback processor. Treat ASIO support as a future engine track, not a
  controller-routing bug.

## Finish Criteria

The Controller can be considered wrapped for the current development phase when
these items are complete:

1. Installed build passes the one-hour validation gate.
2. Manual endpoint recovery is verified with EarPods disconnect/reconnect.
3. Sleep/wake recovery is verified once with the selected listening endpoint.
4. Diagnostics are exported after the manual recovery pass.
5. The full eight-hour gate is run when a convenient test window is available.
6. Code-signing is provisioned before public distribution outside the
   development machine.

## Recommended Sequence

1. Run `Start 1-Hour Validation Test.bat`.
2. If it passes, perform the manual device-recovery pass from
   `SOAK-TESTING.md`.
3. Export Diagnostics from the Controller.
4. Record the newest evidence folder under:

```text
%LOCALAPPDATA%\Axiom\SoakTests
```

5. Defer the eight-hour gate until the machine can stay idle, plugged in, and
   free from Windows Update or audio-device changes.

## Deferred Work

- Native ASIO/ASIO4ALL hosting.
- Custom virtual endpoint driver.
- Public release signing.
- Any new sound-changing Axiom-DSP EEL candidate work.
