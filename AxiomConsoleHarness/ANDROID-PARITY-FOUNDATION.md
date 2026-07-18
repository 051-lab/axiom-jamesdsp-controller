# Android Parity Foundation

This pass starts from Windows baseline `b7aacf3`, Android behavior reference
`051-lab/RootlessJamesDSP@0ee48f9`, and native-core reference
`051-lab/JamesDSPManager@86ff781`.

Scope is reproducible Windows builds, LiveProg/native stability, focused tests,
and CI. Darwin UI work, Android services, and Windows routing changes are not
part of this pass.

## Baseline Build Record

The source worktree was clean at `b7aacf3b5076710335d2da7cd77c79708a1fd265`
before `feature/android-parity-foundation` was created.

The initial host was Linux with CMake 3.28.3 and MinGW GCC 13, but without a
.NET SDK:

- Native configure completed with the MinGW cross toolchain. The baseline
  compile was not a canonical MSVC result: it stopped on the existing EEL
  `max` macro collision with the MinGW C++ standard library and GCC's rejection
  of jumps across initialized variables in the Windows console host.
- The baseline .NET build could not run locally because `dotnet` was absent.
- The canonical clean build is now encoded in `.github/workflows/windows-ci.yml`
  on `windows-latest`: MSVC-compatible native console and native tests, the
  .NET 8 controller, and the .NET metadata tests.

These environment limitations are recorded rather than presented as product
failures. Windows CI is the reproducible build authority for this Windows-only
codebase.

Current-branch verification on the same host:

- The MinGW Windows cross-build produces `AxiomJamesDSPCore` and
  `AxiomJamesDSPNativeTests.exe` without warnings. The executable cannot run on
  this host because no Windows runtime layer is installed.
- The full MinGW console cross-build still stops on baseline C++ portability
  issues in the WASAPI host (`goto` across initialized locals and the EEL
  `max` macro colliding with the MinGW standard library). The canonical MSVC
  console build remains the Windows CI gate.
- .NET SDK 8.0.423 builds the controller with zero warnings and zero errors.
- All five cross-platform LiveProg metadata tests pass.

## Fifteen-File Native Audit

The vendored Windows core and `JamesDSPManager@86ff781` differ in exactly 15
common native files.

| File | Decision |
| --- | --- |
| `cpthread.c` | Retain Windows return-value fixes; the updated core removes required returns from non-void Windows wrappers. |
| `essential.h` | Retain Windows `stdint.h`/`stddef.h` includes. |
| `jamesdsp.c` | Retain explicit Windows initialization/destruction entry points; update only the revised LiveProg parser call. |
| `jdsp/Effects/convolver1D.c` | Port null/channel/length/non-finite validation and allocation cleanup. |
| `jdsp/Effects/crossfeed.c` | Defer the unrelated BS2B default/index behavior difference. |
| `jdsp/Effects/dbb.c` | Defer the unrelated duplicate-assignment cleanup. |
| `jdsp/Effects/dynamic.c` | Port limiter stability and sample-rate handling; remove the stale machine-local debug write. |
| `jdsp/Effects/eel2/cpthread.c` | Retain Windows return-value fixes. |
| `jdsp/Effects/eel2/nseel-compiler.c` | Retain the Windows HEURISTIC fractional-delay guards, SAFETY sample-rate fallback, typed locals, and decoder portability fixes. |
| `jdsp/Effects/eel2/numericSys/FilterDesign/polyphaseFilterbank.c` | Retain the explicit Windows fallback return. |
| `jdsp/Effects/liveprogWrapper.c` | Port section-aware `@slider`/`@block`, `samplesblock`, transactional replacement, compiler detail, and validated variable updates. Remove the older variable-creating setter. |
| `jdsp/Effects/multimodalEQ.c` | Retain existing Windows asynchronous-EQ work, remove stale machine-local diagnostic writes, and defer broader reconciliation. |
| `jdsp/Effects/vacuumTube.c` | Port finite-value, DC-blocking, and sample-rate stability hardening. |
| `jdsp/jdspController.c` | Port variable-block capacity, limiter routing, finite postgain, sample-rate refresh, and mutex cleanup while retaining Windows compatibility includes. |
| `jdsp/jdsp_header.h` | Port revised structs/APIs while retaining Windows forward declarations and asynchronous-EQ declarations. |

The shared Windows pthread headers also receive matching `const` declarations
and an explicit `time.h` dependency, and the Android effect entry source maps
`getpid` to the Windows CRT spelling. These are cross-build compatibility fixes,
not additional Android behavior ports.

## LiveProg Contract

- `@init` is optional; `@sample` is required.
- `@slider` runs once after successful compilation and after each accepted
  variable update.
- `@block` runs once per processing block and `samplesblock` contains the
  current frame count.
- A replacement is compiled into an isolated candidate VM. Only a successful
  compile swaps it into the audio engine.
- Invalid scripts and unreadable replacement files leave the preceding working
  script active.
- Variable names must be valid EEL identifiers, must already exist, and values
  must be finite.

## Non-Routing Verification

On Windows:

```bat
cmake -S AxiomConsoleHarness -B build-axiom-console -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-axiom-console --config Release
ctest --test-dir build-axiom-console -C Release --output-on-failure
dotnet build AxiomConsoleHarness\AxiomJamesDSPController\AxiomJamesDSPController.csproj -c Release
dotnet test AxiomConsoleHarness\AxiomJamesDSPController.Tests\AxiomJamesDSPController.Tests.csproj -c Release
```

The focused native executable covers section lifecycle, `samplesblock`,
validated variables, failed replacement rollback, variable block capacity, and
convolver input validation. The .NET project covers Android-parity metadata
formats, lists, duplicates, section boundaries, finite values, and the
128-parameter limit.
