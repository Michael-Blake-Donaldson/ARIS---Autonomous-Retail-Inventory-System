# Establish the native build and core dependency boundaries

**Status** Accepted

## Context

Phase 1 needs repeatable startup and tests on Windows with a Linux headless verification path. Future autonomy code needs access to typed core contracts without depending on graphics, logging or platform APIs.

## Decision

Use C++20, CMake and Ninja. MSVC builds Windows x64 targets. Ubuntu builds the headless targets with Clang, clang-tidy, ASan and UBSan. vcpkg resolves direct and transitive packages from the manifest baseline. The baseline also identifies the bootstrap checkout used by CI.

The target dependencies point inward:

```text
argus-desktop -> argus_app -> argus_core
argus-headless -> argus_app -> argus_core
argus_unit_tests -> argus_app -> argus_core
```

Only the Windows shell links SDL3 and the system OpenGL library. The startup library owns command-line parsing and fixed-field logging via spdlog. Tests link GoogleTest. OpenGL uses the Windows SDK. Defer additional libraries until a phase requires them.

Use `Result<Value, Error>` with a module-specific error enum for anticipated failures. Strong IDs and quantities represent semantic distinctions, with domain code responsible for validating ranges. Each run owns its own `Random` instance. Raw `std::mt19937` output provides a standardized reference sequence for a 32-bit seed. Its use is limited to simulation. Sampling distributions will need their own determinism contract.

## Alternatives

A platform-specific project file would put Linux verification on a second build system. Downloading packages from floating tags at configure time would make dependency resolution harder to reproduce. A single target would allow CLI, platform and domain concerns to acquire dependencies on each other.

## Consequences

CI pins third-party actions to reviewed SHAs and runs with read-only repository permissions. vcpkg's binary cache is keyed by platform, preset and manifest, with vcpkg validating package ABI compatibility. Only successful default-branch builds can publish those cache entries. Baseline updates require rebuilding all presets and reviewing dependency and license changes. Dependency resolution is repeatable. The compiler, SDK, system libraries and runner image remain host inputs.
