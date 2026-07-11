# Keystone Implementation Plan

> **Status**: Superseded by ADR-006 — this document has been updated to reflect the
> current project scope. The original HMAS agent orchestration plan was migrated to
> **ProjectAgamemnon** when Keystone was decoupled from ai-maestro.

## Executive Summary

Keystone is a C++20 **transport infrastructure library** providing invisible,
zero-configuration message routing for all HomericIntelligence components. It is not
an agent system, pipeline stage, or orchestrator — it is the invisible plumbing
beneath every other component.

The system bridges two complementary transports transparently:

- **Local transport**: BlazingMQ + lock-free `concurrentqueue` for intra-host routing
- **Cross-host transport**: NATS JetStream over Tailscale WireGuard mesh

## Architecture Decision Records

The `adr/` subdirectory contains all architectural decisions:

| ADR | Title |
|-----|-------|
| [ADR-001](adr/ADR-001-message-bus-architecture.md) | MessageBus Architecture |
| [ADR-002](adr/ADR-002-work-stealing-scheduler-architecture.md) | Work-Stealing Scheduler Architecture |
| [ADR-003](adr/ADR-003-priority-queue-anti-starvation-strategy.md) | Priority Queue Anti-Starvation Strategy |
| [ADR-004](adr/ADR-004-coroutine-integration-with-scheduler.md) | Coroutine Integration with Scheduler |
| [ADR-005](adr/ADR-005-message-pool-design.md) | Message Pool Design |
| [ADR-006](adr/ADR-006-agent-interface-type-safety-concepts.md) | Agent Interface Type Safety (Concepts) |
| [ADR-007](adr/ADR-007-shared-ptr-migration.md) | Shared Pointer Migration |
| [ADR-008](adr/ADR-008-async-agent-unification.md) | Async Agent Unification |
| [ADR-009](adr/ADR-009-message-processing-strategy-pattern.md) | Message Processing Strategy Pattern |
| [ADR-010](adr/ADR-010-architecture-issue-resolution.md) | Architecture Issue Resolution |
| [ADR-011](adr/ADR-011-phase-6-architecture-review-fixes.md) | Phase 6 Architecture Review Fixes |
| [ADR-012](adr/ADR-012-cpack-build-system-decisions.md) | CPack Build System Decisions |
| [ADR-013](adr/ADR-013-coroutine-safety-patterns.md) | Coroutine Safety Patterns |

## Project Directory Structure

```
Keystone/
├── include/                         # Public headers
│   ├── agents/
│   ├── concurrency/
│   ├── core/
│   ├── monitoring/
│   ├── network/
│   └── simulation/
├── src/                             # Implementation
│   ├── agents/
│   ├── concurrency/
│   ├── core/
│   ├── monitoring/
│   ├── network/
│   └── simulation/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── e2e/
│   ├── load/
│   ├── fixtures/
│   └── mocks/
├── benchmarks/
├── examples/
├── docs/
│   ├── plan/                        # This directory — implementation plans and ADRs
│   │   └── adr/                     # Architecture Decision Records
│   ├── phases/                      # Phase completion summaries
│   ├── profiling/
│   └── runbooks/
├── conan/                           # Conan 2 dependency definitions
├── cmake/                           # CMake modules and scripts
├── helm/, k8s/                      # Deployment configs
├── monitoring/, grafana/            # Observability configuration
├── schemas/, proto/                 # Message schemas
├── scripts/
├── CMakeLists.txt
├── CMakePresets.json                # Preset-based build configuration (v8)
├── conanfile.py                     # Conan 2 package manifest
├── justfile                         # Developer workflow shortcuts
└── Makefile                         # Alternative build entry point
```

## Performance Targets

| Metric | Target |
|--------|--------|
| Local routing latency | < 500 ns |
| Local throughput | > 2 M msg/sec |
| Cross-host delivery | At-least-once via NATS JetStream |

## What Was Moved to ProjectAgamemnon

The following are **no longer part of Keystone**:

- L0 `ChiefArchitectAgent` (Chief Architect)
- L1 `ComponentLeadAgent` (Component Lead)
- L2 `ModuleLeadAgent` (Module Lead)
- L3 `TaskAgent` (Task Agent)
- HMAS 4-layer hierarchy design and TDD roadmap
- Delegation and escalation logic
- Work-stealing scheduler (agent-level)

See CLAUDE.md at the repository root for the authoritative description of the
current project scope.

---

**Document Version**: 2.0
**Last Updated**: 2026-04-22
**Status**: Current — reflects post-ADR-006 transport-only scope
