# ADR-016: Python Orchestration Migration to ProjectAgamemnon

**Status**: Accepted
**Date**: 2026-04-25
**Deciders**: ProjectKeystone Development Team
**Tags**: architecture, extraction, orchestration, python, projectagamemnon, decoupling
**Related**: ADR-015

## Context

ProjectKeystone originally contained Python orchestration modules alongside its C++ transport layer:

- `dag_walker.py` — DAGWalker (task ready-set computation, cycle detection)
- `task_claimer.py` — TaskClaimer (per-team concurrency guard, drain)
- `nats_listener.py` — NATSListener (NATS subject subscriber, `hi.tasks.>`)
- `maestro_client.py` — MaestroClient (AI Maestro REST client)
- `models.py` — Task, Agent, TaskEvent domain models
- `daemon.py` — orchestration daemon entry point
- `config.py`, `logging.py`, `validation.py` — supporting utilities

These modules were part of the Python subscriber that consumed task events from the `homeric-tasks` NATS stream and advanced the DAG through AI Maestro.

### Problems with Co-location

1. **Language mixing**: ProjectKeystone's CLAUDE.md mandates C++20 only for implementation.
   Python orchestration modules violated this principle.
2. **Scope creep**: Keystone's mandate is invisible, zero-configuration message routing.
   Task scheduling, DAG advancement, and Maestro integration are orchestration concerns
   that belong in a dedicated orchestration system.
3. **Dependency bloat**: Consumers of Keystone's C++ transport library were forced to
   manage Python dependencies (asyncio, aiohttp, pydantic, etc.) even when using only C++.
4. **Testability**: Python orchestration tests required complex async test fixtures and
   were brittle across different Python versions.
5. **Deployment**: Python and C++ components required separate deployment, version,
   and configuration management.

### Relationship to ADR-015

ADR-015 documented the extraction of the C++ agent hierarchy (HMAS 4-layer agents).
This ADR documents the parallel extraction of Python orchestration modules. Together,
they complete the decoupling of ProjectKeystone from all non-transport concerns.

## Decision

Extract all Python orchestration modules from ProjectKeystone into **ProjectAgamemnon**'s
`agamemnon.orchestration` package:

- Move `dag_walker.py` to `agamemnon/orchestration/dag_walker.py`
- Move `task_claimer.py` to `agamemnon/orchestration/task_claimer.py`
- Move `nats_listener.py` to `agamemnon/orchestration/nats_listener.py`
- Move `maestro_client.py` to `agamemnon/orchestration/maestro_client.py`
- Move `models.py` to `agamemnon/orchestration/models.py`
- Move `daemon.py` to `agamemnon/orchestration/daemon.py`
- Move supporting modules (`config.py`, `logging.py`, `validation.py`) to `agamemnon/orchestration/`

ProjectKeystone retains sole ownership of:

- NATS subject schema (`homeric-research`, `homeric-myrmidon`, `homeric-pipeline`,
  `homeric-agents`, `homeric-tasks`, `homeric-logs`)
- Python dependencies limited to: `nats-py` (for subject schema validation only)

ProjectAgamemnon now owns the complete orchestration stack, including:

- DAG traversal and ready-set computation
- Per-team concurrency control
- Task claiming and batch pulling
- Maestro integration
- Configuration and logging

## Consequences

### Positive

- **Pure C++20 mandate**: ProjectKeystone is now exclusively C++20. All transport
  primitives are implemented in C++ with modern language features and strong type safety.
- **Clear language boundaries**: ProjectAgamemnon is now the exclusive home for both
  C++ agent coordination logic and Python orchestration logic.
- **Dependency isolation**: Consumers of Keystone's C++ library do not pull Python
  dependencies (asyncio, aiohttp, pydantic, etc.).
- **Independent evolution**: Orchestration algorithms (DAG traversal, task claiming,
  concurrency control) can evolve independently from transport routing logic.
- **Single responsibility**: Each system owns its complete domain:
  - Keystone: message routing and delivery
  - Agamemnon: task scheduling and DAG advancement
- **CLAUDE.md alignment**: The project description now correctly reflects ProjectKeystone
  as a pure C++20 transport library with no Python components.

### Negative / Trade-offs

- **Cross-system coordination**: Tasks that touch both transport protocol (Keystone) and
  orchestration behavior (Agamemnon) require coordinated PRs in both repositories.
- **Version management**: Agamemnon must pin or track a compatible version of Keystone's
  NATS subject schema and transport contracts.
- **Async/await patterns**: Python async patterns in Agamemnon are now separate from
  C++ coroutine patterns in Keystone, requiring careful interface design at the boundary.

### Neutral

- Related ADR-015 (C++ agent extraction) remains valid and unchanged.
- NATS subject schema remains owned and documented in Keystone; Agamemnon consumes it.

## Validation

### Checklist

- ✅ All Python modules moved to `agamemnon.orchestration`
- ✅ Keystone's `pyproject.toml` updated to remove orchestration dependencies
- ✅ Agamemnon's `pyproject.toml` updated to include orchestration dependencies
- ✅ NATS subject schema documentation retained in Keystone
- ✅ Import paths updated in Agamemnon's orchestration code
- ✅ Python tests migrated to Agamemnon
- ✅ CI/CD pipelines updated to test each repository independently
- ✅ CLAUDE.md updated to reflect Python extraction

## References

- ADR-015: Agent Layer Extraction to ProjectAgamemnon
- HomericIntelligence/Odysseus#143: Move Python orchestration layer from Keystone to ProjectAgamemnon
- HomericIntelligence/ProjectKeystone: NATS Subject Schema (owner: Keystone)
- HomericIntelligence/ProjectAgamemnon: agamemnon.orchestration package

---

**Last Updated**: 2026-04-25
**Version**: 1.0
**Project**: ProjectKeystone
**Status**: Accepted (2026-04-25)
