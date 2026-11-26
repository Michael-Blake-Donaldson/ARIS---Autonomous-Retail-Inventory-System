# Phase 22 final simulation release verification

**Date** November 26, 2026

**Code under test** `6bb7d537212468c69c9be14b4ebf5e8a6f41f1f1`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Operator workflow | Native probes use the 1280 × 720 layout, check horizontal fit, open an audit in two clicks and preserve selection into Maintenance. Location Audits now offers robot selection beside the location search. The updated quick start covers both robots, review, re-audits and interrupted work. |
| Fleet hold and recovery | The native reopening probe interrupts Robot 2 more than 0.9 m from its berth, observes a visible hold and retained aisle claim, clicks **Return to berth**, and verifies the release. Previously completed reports remain available. |
| Shared-aisle regression | The previously timed-out pairing `A20J05` and `A20J07` passed the direct and native integration probes. Robot 2 visibly yielded and completed its audit after Robot 1 cleared the route. The initial integration scenario still covers concurrent whole-shelf and Waco work. |
| Portable replacement | The 22-check acceptance script verified the digest, launched an isolated extraction with a system-only PATH, replaced the program, reopened saved fleet and review evidence, ran berth recovery, and checked that uninstall preserved the data. The bundled copy of the script matched the tested source file. |
| Preview assets | The staged Fleet screenshot and native walkthrough were captured from the extracted package. Visual review checked the fleet map, both location forms, Maintenance, and side-by-side DPCI counts. FFprobe decoded 271 H.264 frames at 1280 × 720 and 20 fps. |
| Separate Windows installation | **Open.** Windows Sandbox is unavailable and the current session lacks Hyper-V access. Local package verification uses fresh extracted program folders and isolated operator data under a system-only PATH on Windows 11 Pro build 26200. The detected NVIDIA GeForce RTX 4070 driver was `32.0.16.1742`. |

## Commands and results

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | 129/129 Windows Release tests passed, including both native desktop suites. |
| `tools/test-windows-package.ps1 -NativeUi` | 22/22 extracted-package checks passed. Archive SHA-256 `0ba0032a75a022cfdf1cd52509364ed85bbe4b48b1166a5a14de0fc565ba81ed`. |
| `cmake --workflow --preset windows-debug` | 127/127 passed. |
| `cmake --workflow --preset linux-ci` | 126/126 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Passed. The declared link graph is acyclic across 26 targets including optional fuzz targets. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Extracted Phase 21 and Phase 22 builds produced byte-identical stdout with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The staged package, checksum, receipt, screenshot and walkthrough are in `out/release/phase-22`. The Windows binaries are unsigned.

## Reproduce on a separate Windows installation

Use an interactive Windows 11 x64 desktop and PowerShell 7.2 or later. Obtain the archive digest from the trusted handoff. Verify it before extracting the packaged acceptance script.

```powershell
$zip = (Resolve-Path '.\ARGUS-0.1.0-windows-x64.zip').Path
$expected = (Read-Host 'Expected SHA-256 from trusted handoff').Trim()
if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash $zip).Hash -ne $expected) {
    throw 'Package digest mismatch'
}
$destination = Join-Path $PWD ('ARGUS-verify-' + [Guid]::NewGuid())
Expand-Archive -LiteralPath $zip -DestinationPath $destination
& (Join-Path $destination 'verification/test-windows-package.ps1') `
    -Package $zip -ExpectedSha256 $expected -NativeUi |
    Set-Content -LiteralPath acceptance.json -Encoding utf8
```

Record the installation's provenance with the receipt. The two-robot walk-through and the simulation and RFID limits are in the release notes. [Release walkthrough](../release/release-page.md) · [Windows quick start](../release/onboarding.md)
