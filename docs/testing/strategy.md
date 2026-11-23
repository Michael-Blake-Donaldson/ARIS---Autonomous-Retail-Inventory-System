# ARGUS test strategy

- Unit tests validate typed core values, deterministic RNG sequences, and configuration parsing at every boundary. Use GoogleTest and fixed independent test seeds.
- Executable smoke tests run the built binaries and assert exit status and key output. Keep GUI initialization out of unattended smoke tests unless a display and graphics context are available.
- Exercise the native window on Windows and record the graphics and session limits.
- Verify changes with Windows x64 Debug and Release and Linux x64. Run formatting, clang-tidy and ASan/UBSan on the Linux verification job.
- As simulation phases arrive, add integration tests with frozen scenarios, seeds and configuration. Keep ground truth comparisons inside evaluation tests.
- Record commands, toolchain versions, outcomes, gaps, and failures in each phase report.
