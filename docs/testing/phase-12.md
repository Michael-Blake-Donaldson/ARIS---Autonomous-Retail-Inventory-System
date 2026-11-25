# Phase 12 Windows packaging and release verification

**Date** November 25, 2026

**Release candidate** ARGUS 0.1.0, unsigned portable ZIP for Windows x64

## Acceptance gates

| Gate | Status and evidence |
| --- | --- |
| Installs and launches on a fresh Windows machine without an IDE | **Open.** The extracted package launches and performs the workflows with a system-only PATH on Windows 11 Pro build 26200. That host already has Visual Studio. A separate clean Windows installation is required. Hyper-V access was denied to the current account and Windows Sandbox was unavailable. |
| Core workflows, saved results, and export validation | **Passed locally.** The portable acceptance script exercised 14 checks. It authored a sample warehouse, deployed the robot, explored and mapped, searched the catalog, audited and reopened results, and resumed access to the saved mission after replacing the extracted program. The native UI tour also passed. |
| Package documentation, integrity and evidence | **Passed locally.** The ZIP includes examples, package notices, build provenance, passing test results, and 10 vcpkg SPDX documents. Its SHA-256 sidecar matched and a wrong hash was rejected before extraction. Repacking unchanged inputs under the recorded source epoch produced identical bytes. |
| Tag workflow and GitHub release page | **Open.** The tag-triggered workflow and local release notes, screenshot, and clip are ready. Hosted CI has no run for the unpublished candidate. The existing public remote points to the older prototype. Owner authorization to distribute the release source and publish the GitHub release is pending. |

## Build and verification commands

Use the Visual Studio x64 developer environment, the pinned vcpkg checkout and PowerShell 7.2 or later. This machine configured CMake 3.28.3-msvc11 and MSVC 19.40.33811.0. The display adapter was an NVIDIA GeForce RTX 4070. Package test outputs and the archive digest are retained in `out/release`. Build and media logs are under `out/evidence/phase-12`.

```powershell
pwsh -NoProfile -File tools/build-windows-package.ps1 -NativeUi
$zip = 'out/release/ARGUS-0.1.0-windows-x64.zip'
$digest = (Get-Content -LiteralPath "$zip.sha256" -Raw).Trim().Split(' ')[0]
./tools/test-windows-package.ps1 -Package $zip -ExpectedSha256 $digest -NativeUi
```

| Check | Observed result |
| --- | --- |
| Windows portable Release configure and build, followed by CTest | 106/106 passed, including inventory schema migration, corrupt inputs, cancellation races, the 10,000-tag capacity test and the interactive native operations tour. |
| Portable acceptance from a standalone extraction | 14/14 checks passed. The package launched with the working directory outside its program folder and a system-only PATH. |
| Installed native tour and graphics rebuild | Passed on the verified archive. The per-run foreground timing and visible catalog row count are recorded in the acceptance JSON. |
| SHA-256 negative test | A deliberately incorrect digest failed before extraction. |
| Runtime imports and signatures | `dumpbin /dependents` found only packaged DLLs or Windows system imports. Windows reported all four packaged executables as unsigned. |
| SPDX checksum manifest | Verified the SHA-256 digest of all 10 included SPDX documents. |
| MSVC project-code analysis | The previously configured analysis targets remained buildable. Phase 12 changed build, packaging, release tests and docs. |
| Package script and CI configuration | PowerShell parser validation, C++ format check and Git diff checks passed. YAML parsing confirmed the tag trigger, read-only repository permissions and 30-day artifact retention. |

The archive includes four ARGUS executables, the vcpkg and MSVC runtime DLLs, fictional examples, and CTest receipt. It has no system installer. Extraction, separate-directory replacement, and removal are the supported install lifecycle. The tests denied writes on a controlled directory with a Windows ACL and verified that package removal preserves external operator data. They exercised SQLite inventory migration from schema 1 to 2 and rejected a future schema through the bundled test suite. This is the first package version. The replacement workflow reused the current binary version with existing data.

The verified map was reopened with a pinned SHA-256, and one altered byte caused rejection. The audit JSON parsed with four expected units and one unique EPC attributed to the sampled aisle. The CSV reopened with a likely-misplaced review row. The mission checkpoint reopened with four covered views after removing the first program installation.

## Performance and media

The simulator and C++ runtime behavior retain the Phase 11 measurements. The [benchmark, sanitizer and fault report](phase-11.md) records the workload, hardware and observed limits. The packaged desktop captured its own framebuffer at 1280 × 720 and 20 fps. FFprobe verified a 579-frame H.264 clip lasting 28.95 seconds. The local release media includes a captured Missions view and the native tour. The [release notes](../release/release-page.md) include a two-minute demonstration script and links to the walkthrough.

## Failures and limits

- The first metadata extraction missed CRLF in the generated CMake compiler record. The packaging driver accepts both line endings. The first acceptance script used an unsupported numeric separator in a method argument. The corrected runner passed from an extracted ZIP.
- An initial recording command used a flag unsupported by the installed FFmpeg. Retrying with supported flags produced the verified MP4.
- The Windows Debug scale check and the Linux mission CLI exceeded their 30-second outer test timeouts. These two integration checks now have a bounded 90-second CTest budget. The fixture workload, individual mission subprocess timeouts, and application runtime limits stay in force. The phase logs retain the initial attempts.
- Reproducing the ZIP from identical inputs used the same build and evidence. Independently rebuilt archives can differ with toolchain versions and measured test durations.
- The simulation carries no real-world robot safety certification. RFID readings are probabilistic and uncalibrated. There is no proprietary retailer integration. The package remains unsigned. Public redistribution terms and product identity are owner decisions. [Distribution notice](../release/NOTICE.md)
