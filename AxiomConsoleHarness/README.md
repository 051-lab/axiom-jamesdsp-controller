# JamesDSP Controller for Windows

A Windows controller and WASAPI host for JamesDSP. LiveProg is script-driven:
select an EEL2 file and the controller builds its parameter UI from the script's
metadata instead of assuming a particular Axiom program.

## Current capabilities

- Loads arbitrary LiveProg EEL2 scripts containing `@sample` and any optional
  combination of `@init`, `@slider`, and `@block`.
- Reads RJDSP-style metadata declarations and creates range
  slider/numeric controls or option-list controls dynamically.
- Sends parameter changes through `LiveProgSetVariable`; it never rewrites the
  selected EEL source file.
- Remembers parameter values separately for each script.
- Preserves legacy Axiom profiles, settings, and environment overrides.
- Includes the optional Darwin filter module: validated filter packages,
  256-tap Q31 normalization, optional harmonic processing, automatic headroom,
  and a dedicated secondary JamesDSP engine with a short transition between
  filters.
- Manages WASAPI routing, profiles, host effects, diagnostics, startup, and
  processor recovery.

Fresh installations start with LiveProg and Darwin disabled and no file
selected. The bundled Axiom script remains available as an optional
compatibility/example program.

## Run from source

Build the native processor:

```bat
build_axiom_console.bat
```

Run the WinForms controller:

```bat
run_axiom_controller.bat
```

The source directory and legacy script names retain their existing names to
avoid breaking development workflows. User-facing binaries are
`JamesDSPController.exe` and `JamesDSPConsole.exe`.

## LiveProg metadata

The controller recognizes RJDSP-style declarations such as:

```eel
// @section Dynamics
// @slider gain "Input gain (dB)" -12 12 0.5 0
// @slider mode "Mode" 0 2 1 0 "Clean|Warm|Dense"

@slider
input_gain = gain;

@sample
spl0 *= 10^(input_gain / 20);
spl1 *= 10^(input_gain / 20);
```

Rules enforced by the parser:

- parameter identifiers must be valid EEL identifiers;
- ranges must be finite and have a positive step;
- defaults must be inside the declared range;
- option lists use zero-based, unit-step indexes whose range exactly matches
  the option count;
- a literal assignment can provide the initial value when the declaration
  omits one.

Unsupported or malformed declarations are skipped and reported in the
LiveProg tab. Selecting a bad script does not alter its source.

## Darwin filter packages

The Darwin tab accepts `.zip` or `.darwin` packages containing:

- `filter.json` with a non-empty `list`;
- one or more root-level `.flt` files named by the manifest;
- exactly 256 little-endian signed Q31 coefficients per filter.

The package reader rejects path traversal, duplicate entries, invalid sizes,
unsafe normalization, and non-finite output. The selected filter is exported
as a private float WAV under the controller runtime directory. The native host
loads it into a dedicated JamesDSP engine; the ordinary convolver, tube,
limiter, and post-gain stages are not duplicated while Darwin owns the output.
If a replacement is invalid, the last working Darwin filter remains active.

## Runtime paths and migration

Packaged builds store mutable data under:

```text
%LOCALAPPDATA%\JamesDSP\Controller
```

On first use, an existing
`%LOCALAPPDATA%\Axiom\JamesDSPController` directory is copied forward when the
new location does not yet exist. Legacy profile `AxiomValues` are promoted to
LiveProg parameters.

Preferred development overrides:

- `JAMESDSP_HARNESS_ROOT`
- `JAMESDSP_DATA_ROOT`
- `JAMESDSP_CONSOLE_EXE`
- `JAMESDSP_BUNDLED_EEL`

The legacy `AXIOM_*` equivalents remain accepted.

## Package and installer

Create a self-contained package and optional zip:

```bat
publish_axiom_app.bat -Zip
```

Outputs:

```text
dist\JamesDSPController-win-x64\
dist\JamesDSPController-win-x64.zip
```

Create the installer:

```bat
build_installer.bat
```

The installer upgrades the existing product in place, installs under
`Program Files\JamesDSP Controller`, and preserves Local AppData on uninstall.
An upgrade can retain the prior Program Files directory chosen by the existing
installation; validation scripts detect either location.
Release signing remains a separate step documented in
[`SIGNING.md`](SIGNING.md).

## Verification

Managed parser/package tests:

```bat
dotnet test AxiomJamesDSPController.Tests\AxiomJamesDSPController.Tests.csproj -c Release
```

Native build and tests:

```bat
build_axiom_console.bat
ctest --test-dir ..\build-axiom-console --output-on-failure -C Release
```

Windows package smoke tests:

```bat
tests\run_smoke_tests.bat
```

Long-running routing and recovery checks are described in
[`SOAK-TESTING.md`](SOAK-TESTING.md).

## Audio route

The intended system-wide route is:

```text
Windows applications
  -> virtual playback endpoint (normally VB-CABLE)
  -> JamesDSPConsole WASAPI loopback capture
  -> JamesDSP processing
  -> selected physical listening device
```

The capture and output endpoints must be different. VB-CABLE is external and
is not redistributed with this project.
