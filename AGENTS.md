# ARGUS engineering contract

- Use the engineering plan in the repository root as the phase acceptance specification. Implement one approved phase at a time and stop at its handoff.
- Preserve the simulation boundary. Ground truth belongs to world simulation and sensor generation. Operational code consumes observations and estimates. Any truth diagnostics or evaluators must be explicit and isolated.
- Use C++20, RAII, explicit ownership, strong types for units and IDs, and dependency injection for per-run random state. Verify target dependencies stay acyclic.
- Treat input as untrusted. Bound resource use before allocation or processing, validate data before it enters domain code, keep paths constrained, and use parameterized database statements when persistence is introduced. Use fictional inventory data and keep credentials out of the repository and logs.
- Keep changes reviewable. Use the [review checklist](docs/review-checklist.md), record consequential choices in ADRs, and document interfaces and failure modes.
- Run the applicable build, tests, formatting, and static analysis. Record commands, outcomes, limits, and open gates in `docs/testing/phase-XX.md`. Mark gates passed only with evidence.
