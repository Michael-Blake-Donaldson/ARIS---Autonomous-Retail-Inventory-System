# Windows release candidate procedure

From a clean checkout, use an x64 Visual Studio Developer PowerShell with PowerShell 7.2 or later, CMake, Ninja, Git and the exact vcpkg checkout specified in the manifest. Bootstrap vcpkg and set `VCPKG_ROOT` as described in the [repository build guide](../../README.md). Run the packaging driver. Add `-NativeUi` only on an interactive desktop.

```powershell
pwsh -NoProfile -File tools/build-windows-package.ps1 -NativeUi
$package = 'out/release/ARGUS-0.1.0-windows-x64.zip'
$sha256 = (Get-Content -LiteralPath "$package.sha256" -Raw).Trim().Split(' ')[0]
./tools/test-windows-package.ps1 -Package $package -ExpectedSha256 $sha256 -NativeUi |
    Set-Content -LiteralPath out/release/acceptance.json -Encoding utf8
```

`verification/build.json` records the exact source commit and timestamp, registry baseline, CMake, MSVC, Windows version, build preset and signing status. `verification/ctest.json` contains observed case results and durations without the build users' paths. Per-port SPDX documents have a checksum manifest. CPack uses the commit timestamp as `SOURCE_DATE_EPOCH`. Test durations, toolchains and generated package metadata can affect independently rebuilt bytes. The distribution digest identifies a particular archive.

On a fresh Windows 11 x64 machine with an interactive OpenGL-compatible driver, copy the ZIP, hash and acceptance script. Run the same check with a trusted expected digest and zero pre-existing ARGUS or IDE setup. Capture the machine's OS and graphics driver in the verification record. Confirm data persists across removal and replacement before publishing. The same command with no `-NativeUi` runs the headless workflows.

Version tags must match both version declarations and point to the source that was tested. The Windows workflow verifies the tag, builds with the pinned registry, runs staged package acceptance and retains the candidate for 30 days. GitHub-hosted Windows runners exercise the isolated extracted package with a system-only PATH. Use a separate clean Windows machine for the fresh-install graphics gate.

Before distributing, verify the source and runtime rights and package notices, confirm product identity, and review the acceptance findings. Use [release-page.md](release-page.md) for release notes and the demonstration script. Stage the package, checksum, acceptance result, screenshot, and video as draft release assets for review. Resolve count or digest mismatches by rebuilding from the intended clean revision. [Dependency and licensing review](../security/dependencies.md)
