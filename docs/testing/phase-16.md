# Phase 16 simulation showcase verification

**Date** November 25, 2026

**Packaged source** `1233d461217370dcc2246934deab87fc24350b85`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Continuous desktop workflow | The packaged native tour verified observed robot movement, LiDAR dropout hold and recovery, ongoing routed work, saved map and audit review, resize and OpenGL context recreation. The tour kept truth in the explicit diagnostics view. |
| Portable replacement and data retention | The harness extracted the verified ZIP, ran the desktop with external data, removed the first program directory, and reopened its archived map and audit in a fresh process from the replacement. Removing the replacement preserved the external desktop databases, CLI map and journal. |
| CLI walkthrough and fault handling | The same extracted package ran a custom scenario, drove and explored, exported and pinned a map, rejected corrupt map data and a denied write, looked up a product, completed an investigation and audit, and reopened the mission report after replacement. A mismatched package hash was rejected before extraction. |
| Build and supply-chain evidence | The ZIP includes app-local runtimes, examples, dependency notices, 10 SPDX files, pinned vcpkg provenance, and a CTest receipt. Windows reported the desktop executable unsigned. |
| Standalone extraction on the verification host | Portable acceptance passed 17 checks on Windows 11 Pro x64 build 26200. Packaged executables used a system-only PATH and a working directory outside the program folder. |
| Separate fresh Windows installation | **Open.** The verification host already has Visual Studio. The account could not enumerate Hyper-V VMs, and Windows Sandbox was unavailable. |

The packaged desktop tour rendered 788 frames, drew occupancy on 758 frames and rebuilt geometry 144 times. The same run reported 0.434 ms active frame p95 and 0.691 ms p99. The advance/rebuild counts are from this particular tour. Use the matched before-and-after trials and counter limitations in the [Phase 15 measurements](phase-15.md).

## Reproduce

Run from the repository root in an interactive Visual Studio x64 developer environment with the vcpkg registry pinned in `vcpkg.json`.

```powershell
pwsh -NoProfile -File tools/build-windows-package.ps1 -NativeUi -OutputDirectory out/release/phase-16
$zip = 'out/release/phase-16/ARGUS-0.1.0-windows-x64.zip'
$sha = (Get-Content -LiteralPath "$zip.sha256" -Raw).Trim().Split(' ')[0]
./tools/test-windows-package.ps1 -Package $zip -ExpectedSha256 $sha -NativeUi
```

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | Windows x64 Release passed 113/113 CTest entries including native operations. CPack built the ZIP. |
| `tools/test-windows-package.ps1 -NativeUi` | 17 acceptance checks passed including desktop reopening after replacement and data preservation after removal. |
| `tools/test-windows-package.ps1` without `-NativeUi` | 14 standalone CLI acceptance checks passed. |
| `cmake --workflow --preset windows-debug` | 112/112 tests passed. |
| `cmake --workflow --preset linux-ci` | 111/111 tests passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` targets were up to date. Phase 16 changed no C++ source. |
| PowerShell parser, `clang-format-18 --dry-run --Werror`, and `git diff --check` | Passed for the portable harness, tracked C++ and phase diff respectively. |

Repacking with CPack at the recorded `SOURCE_DATE_EPOCH` reproduced the original ZIP digest. An earlier repack picked up this draft report through CPack's documentation directory rule. The identical-input repack excluded the post-package report.

## Local artifacts

The local handoff is under `out/release/phase-16`. The ZIP and native media are separate files. The acceptance receipt and this report were produced after packaging.

| Artifact | Evidence |
| --- | --- |
| `ARGUS-0.1.0-windows-x64.zip` | SHA-256 `1263c276a056bfe0424807e1194dc54ace269537a6cb054a54494e2b973adbb1`. |
| `acceptance.json` | Package digest, packaged commit, native tour and fresh-process reopen results, and passed checks. |
| `ARGUS-0.1.0-workflow-tour.mp4` | Recorded from the verified extracted executable on a system-only PATH. FFprobe decoded 796 H.264 frames at 1280 × 720 and 20 fps for 39.80 seconds. |
| `ARGUS-0.1.0-operations-center.png` | Frame captured at 38 seconds shows the saved occupancy map, run and session identifiers, and archived audit JSON. |
| `SHA256SUMS` | ZIP, native video, still image and acceptance receipt digests. |

The capture used the [native recording command](../operations-center.md#local-verification-and-recording). The host's FFmpeg rejected the optional banner flag in an encoder probe. The final capture used supported flags and completed successfully. Visual review checked the stale-scan hold and recovery, product search, audit in progress, completed mission, diagnostics and archived audit JSON in the encoded recording.

The project is an offline simulation with fictional inventory. RFID is probabilistic and uncalibrated for physical operation. The package is unsigned. The artifacts are prepared locally for the [showcase walkthrough](../release/release-page.md).
