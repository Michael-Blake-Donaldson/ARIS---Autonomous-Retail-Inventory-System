# 0012 Package the Windows preview as a portable ZIP

**Status** Accepted for Phase 12

## Context

ARGUS needs a reproducible Windows deployment that runs without an IDE. Package files and operator databases have different lifecycles. The project has an existing public remote, local implementation commits, and no established general redistribution license or app signing certificate.

## Decision

Build a Release x64 ZIP with CPack and app-local MSVC and vcpkg DLLs. Bundle fictional examples, component notices, operator documentation, passing test results and pinned source provenance. Use an independent SHA-256 sidecar. Treat extraction into a new directory as installation. Keep persistent data in an operator-owned directory and preserve it when removing the executable files.

The tag workflow builds and verifies a candidate with read-only repository permissions. Retain it as a CI artifact for up to 30 days. Public distribution requires a separate owner release decision after checking source rights, runtime redistribution terms, product identity and the fresh-machine evidence.

## Consequences

Standard users can extract and remove the package without system-wide installation. Application-local runtime servicing occurs through replacement packages. Operators can move to a new program folder and retain their data backups. The package ships unsigned. A hash checks its bytes against an expected reference obtained through a trusted channel.
