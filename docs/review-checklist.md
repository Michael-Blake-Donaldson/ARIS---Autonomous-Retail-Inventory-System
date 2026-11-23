# Code review checklist

- [ ] The diff implements the selected phase and keeps target dependencies acyclic.
- [ ] Interfaces express units, ownership, fallible results, and data provenance.
- [ ] Resource limits, invalid inputs, cleanup, and failure paths are covered.
- [ ] Operational logic keeps simulated truth outside its interfaces.
- [ ] Logs and fixtures contain no secrets or real inventory or personal data.
- [ ] Dependencies and automation are pinned and permissions are scoped.
- [ ] Applicable builds, tests, formatting, static analysis, and sanitizers ran. The phase report records failures and exceptions.
- [ ] Consequential decisions, interface changes, and known limitations are documented.
