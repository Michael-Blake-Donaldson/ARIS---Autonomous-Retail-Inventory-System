# Dependency and release review

ARGUS uses the vcpkg registry at `9eec002df1cd109f8f104cd966a763daf9751489`. The manifest, registry commit, enabled features, target triplet and build platform together specify the dependency set. The registry pins source revisions and hashes. The CI workflow pins GitHub Actions to commits and disables checkout credential persistence.

| Port | Resolved version | License declared by the pinned port | Use |
| --- | --- | --- | --- |
| fmt | 12.2.0#1 | MIT | spdlog formatting |
| gtest | 1.18.0 | BSD-3-Clause | Tests |
| imgui | 1.92.9 | MIT | Windows desktop |
| nlohmann-json | 3.12.0#2 | MIT | JSON decoding and output |
| picosha2 | 1.0.1 | MIT | Snapshot SHA-256 |
| sdl3 | 3.4.18 | Zlib, MIT, Apache-2.0, BSD-3-Clause | Windows desktop |
| spdlog | 1.17.0#1 | MIT | Structured diagnostics |
| sqlite3 | 3.53.4#2 | blessing | Local persistence |
| vcpkg-cmake | 2025-08-07 | MIT | Build helper |
| vcpkg-cmake-config | 2026-07-21 | MIT | Build helper |

This table comes from the installed manifest and pinned registry port metadata. For distribution, preserve the applicable notices installed as `share/<port>/copyright`. Track the Windows system runtime, graphics API and any bundled redistributables separately in the release inventory.

## Collect the dependency SBOM

Run the commands after a clean manifest installation for the relevant build and use a fresh output directory for each release. The collector copies vcpkg's SPDX 2.3 per-port documents and records SHA-256 for each file. It includes installed build and test ports along with runtime dependencies. [Microsoft vcpkg SBOM reference](https://learn.microsoft.com/en-us/vcpkg/reference/software-bill-of-materials)

```text
cmake -DARGUS_BUILD_DIR=out/build/windows-release -DARGUS_TRIPLET=x64-windows -DARGUS_SBOM_DIR=out/evidence/phase-11-sbom/windows -P tools/collect-vcpkg-sbom.cmake
cmake -DARGUS_BUILD_DIR=out/build/linux-ci -DARGUS_TRIPLET=x64-linux -DARGUS_SBOM_DIR=out/evidence/phase-11-sbom/linux -P tools/collect-vcpkg-sbom.cmake
```

Verify each directory with `sha256sum -c SHA256SUMS`. Record the source commit, build preset, compiler, triplet, platform and release artifact hashes alongside the SBOM in the release evidence.

## Update and release

1. Review upstream security advisories, release notes, license changes and affected input paths. Record the reason for the registry or action update.
2. Review the new vcpkg baseline and source hashes in a dedicated commit. Generate both dependency sets and compare versions, package features, licenses, and file changes.
3. Run the applicable Windows and Linux workflows, parser fuzzing and native UI smoke. Exercise database migrations on copies of known-good fixtures. Compare performance on the same host and explain material changes.
4. Store release artifacts, package notices, SBOMs and verified hashes together. Retain the last known-good release and a verified pre-migration database backup for recovery.
