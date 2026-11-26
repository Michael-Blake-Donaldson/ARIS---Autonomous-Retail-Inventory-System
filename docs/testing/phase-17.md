# Phase 17 fleet and shelf audit verification

**Date** November 26, 2026

**Packaged source** `54a3070d3efbcbbe5336e6c062722d48b3b13c9f`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Fleet route and independent audit work | Windows native probe saw both assigned shelf jobs active with navigation progress. The two engines held different run IDs. Fleet reads both estimates and the selected robot's route. |
| Dedicated maintenance view | The probe selected each robot and verified a fresh LiDAR frame, a motion trail and measured translation with both render overlays enabled. The maintenance view uses the existing renderer's LiDAR and trail colors. |
| Shelf-level status | The probe began with two pending shelves, observed both in progress and waited for both complete. Coverage requires both surveyed viewpoints to meet the RFID frame threshold. Count discrepancies have a separate review flag. |
| Product evidence | The probe validated all three showcase photo textures, the expected shoe with zero attributed reads and product evidence on both shelves. Unit tests round-trip sorted product records and their EPCs and reject malformed, duplicate or inconsistent data. |
| Restart and retention | A new process loaded both completed shelves with their original robot and run provenance in the same data directory. Store tests exercised schema 1 and 2 migration, duplicate and conflicting retries, separate shelf histories, retention and invalid locations. |
| Dependency and observation boundaries | The link graph across 22 declared project targets was acyclic. The fleet controller consumes the existing estimated pose, map, observed route and sensor preview. Shelf locations and inspection viewpoints are operator configuration. |

## Reproduce the two-process check

```powershell
$data = New-Item -ItemType Directory -Path (Join-Path 'out/evidence/phase-17' ('fleet-' + [Guid]::NewGuid()))
out/build/windows-release/argus-desktop.exe --ui-fleet-smoke $data.FullName
out/build/windows-release/argus-desktop.exe --ui-fleet-reopen $data.FullName
out/build/windows-release/argus-desktop.exe --operations-data $data.FullName
```

Select **Location Audits** to view the saved reports. For a manual run, select the two robots in **Fleet**, start each assigned audit and switch to **Robot Maintenance** while the robots are moving. The [operations center guide](../operations-center.md#run-a-two-robot-shelf-audit) documents the workflow.

An independent read-only SQLite check of the native smoke data returned these complete product counts, using DPCI order within each shelf.

| Shelf | Robot | DPCI | Product | Expected | Observed |
| --- | ---: | --- | --- | ---: | ---: |
| A10J05 | 1 | 990-01-0001 | North Loop, cotton tee | 4 | 1 |
| A10J05 | 1 | 990-02-0001 | Juniper canvas shoe | 2 | 0 |
| A20J05 | 2 | 990-01-0001 | North Loop, cotton tee | 0 | 1 |
| A20J05 | 2 | 990-03-0001 | Orbit pocket radio | 6 | 1 |

## Build and verification commands

| Command | Result |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 115/115 passed. |
| `tools/build-windows-package.ps1 -NativeUi` | Windows Release passed 117/117, including the prior operations tour and the two-process fleet test. |
| `tools/test-windows-package.ps1 -NativeUi` | All 17 portable acceptance checks passed against the frozen ZIP. Separately, `--ui-fleet-smoke` and `--ui-fleet-reopen` both passed from the verified extraction with a system-only PATH and external data directory. |
| `cmake --workflow --preset linux-ci` | 114/114 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. Rebuilding after the final report sorting change passed all 14 targeted shelf, mission store and archive tests. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` build passed. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Passed. |

The first Windows run exposed a stale schema-migration test setup, which now removes schema 3 objects before modeling a version 1 database. A later run passed fleet acceptance and timed out during the legacy tour's initial motion. The scripted tour now retries Explore after a transient hold and still requires observed travel. The final 117-test run passed both tours.

The frozen package under `out/release/phase-17-verified` has SHA-256 `eef37335708ea3bd0619d0c2d353071f1e65923823621d4382d808bc407acd83`. A preliminary CPack run included a draft of this post-package report. The frozen repack excluded it, and its digest was checked against the portable acceptance receipt. Raw verification logs are in `out/evidence/phase-17`.

The native UI began rendering and accepted user input during a manual inspection. The computer-use tool detected that input, so the automated navigation was left to the app's scripted native probes. The product resource thumbnails were inspected separately.

## Product photos

Built-in ImageGen produced studio photos for the fictional items. Each prompt requested a centered square ecommerce photograph on a light slate-gray seamless background, with soft even light, a generous border, one item and no props, text, brands or watermark. The product subjects were a folded muted navy cotton crew-neck tee, a cream canvas low-top shoe with cream laces and a gum sole, and a charcoal and warm-orange pocket radio with a grille and tuning dial. The approved 104 × 104 thumbnails are [990-01-0001](../../assets/inventory/photos/990-01-0001.bmp), [990-02-0001](../../assets/inventory/photos/990-02-0001.bmp) and [990-03-0001](../../assets/inventory/photos/990-03-0001.bmp). Windows loads them from the executable's embedded resources.

## Limits

The two robots use independent instances of the same authored layout. This phase verifies concurrent work, observations and reports in the desktop. Inter-robot collision and RF-interference modeling require a shared simulation. The fixture has one physical stock shelf per site. Durable history requires an existing operator data directory. Fresh-machine Windows verification remains open.
