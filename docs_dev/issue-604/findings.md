# Findings: GitHub issue #604

- `AGENTS.md:324-342` already records Keystone's six transport invariants; the new
  Design Philosophy should explain the engineering decision lens rather than
  repeat or rename those invariants.
- `AGENTS.md:10-16` and ADR-015 establish that Keystone is pure transport and that
  agent/orchestration concerns moved to ProjectAgamemnon.
- ProjectOdyssey's `AGENTS.md` carries KISS, YAGNI, DRY, SOLID, modularity, and
  POLA as the inherited development-principle set.
- Hephaestus issue #2111 is the completed ecosystem precedent: it adds a
  repository-specific `## Design Philosophy` section, explicitly attributes
  ProjectOdyssey, and protects the contract with a focused regression check.
- Keystone-specific evidence for those principles exists in ADR-015's
  single-responsibility extraction, `core::IMessageSink` dependency inversion,
  transparent local/remote routing, centralized NATS subject ownership, and
  pull-based durable backpressure.
- Keystone has no Python runtime/test harness. Documentation contract checks are
  shell-based (`justfile:100-113`), and Markdown syntax is gated by the
  `markdownlint` job in `.github/workflows/_required.yml:338-354`.
