# Phase 1 threat model

The executable entry points accept process arguments. CI builds externally supplied pull request changes on ephemeral hosted runners. Phase 1 uses fictional schema examples and has no application network, persistence, or scenario import.

| Entry point | Threat | Control and verification |
| --- | --- | --- |
| CLI | Invalid numeric values, excessive arguments or misleading logs | Reject unsupported argument shapes, inspect at most 11 bytes per argument, parse unsigned decimal with full-consumption and overflow checks, and log stable codes without raw inputs. Test malformed and oversized seeds and invalid flag prefixes. |
| Desktop runtime | Graphics initialization or event failures | Scope SDL, window and OpenGL lifetimes with RAII. Return stable runtime failure codes. Force video initialization failure in the CLI smoke test. |
| Software supply chain | Dependency drift, malicious actions or untrusted cache writes | Pin the vcpkg registry and action revisions, use vcpkg port hashes, grant only contents read access, avoid persisted checkout credentials, and limit shared cache publication to successful default-branch builds. |
| Native code | Memory and undefined-behavior defects | Compile project code with warnings as errors. Enable MSVC SDL checks and Control Flow Guard. Run clang-tidy and ASan/UBSan with leak detection on Linux. |

The process trusts the caller's operating-system environment and the installed Windows graphics stack. SDL/OpenGL are outside the Linux sanitizer run. Reassess the trust boundary and add bounded file handling when Phase 2 introduces scenario imports.
