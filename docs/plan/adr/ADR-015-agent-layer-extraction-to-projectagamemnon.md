# ADR-015: Agent Layer Extraction to ProjectAgamemnon

**Status**: Implemented
**Date**: 2026-03-28
**Deciders**: Keystone Development Team
**Tags**: architecture, extraction, agent-hierarchy, transport, projectagamemnon, decoupling

## Context

Keystone originally contained the full 4-layer HMAS (Homeric Multi-Agent System)
agent hierarchy alongside its transport primitives:

- **L0** — `ChiefArchitectAgent`: Top-level orchestrator
- **L1** — `ComponentLeadAgent`: Component-level coordination
- **L2** — `ModuleLeadAgent`: Module-level management
- **L3** — `TaskAgent`: Leaf-level task execution

This hierarchy was co-located with the transport layer (MessageBus, NATSListener,
NatsConnection, TransparentBridge) during early development because the agent and
transport code were built concurrently. As the project matured it became clear that
conflating agent orchestration logic with transport infrastructure violated the
single-responsibility principle and created coupling that made both subsystems harder
to evolve independently.

### Problems with Co-location

1. **Coupling**: Agent delegation, escalation, and work-stealing logic depended on
   internal transport APIs, preventing the transport layer from evolving its interface
   without coordinating agent changes simultaneously.
2. **Scope creep**: Keystone's mandate is invisible, zero-configuration message routing.
   Agent orchestration is a separate concern that does not belong in a transport library.
3. **Testability**: Integration tests required spinning up both the full agent hierarchy
   and the transport stack, increasing test setup complexity and flakiness.
4. **Deployment granularity**: Consumers of the transport layer were forced to take a
   transitive dependency on the entire agent hierarchy even when they needed only routing
   primitives.
5. **Velocity**: Changes to scheduling or delegation policies required rebuilding and
   retesting the transport layer, and vice versa.

## Decision

Extract all agent hierarchy code from Keystone into a dedicated repository,
**ProjectAgamemnon**, which owns orchestration, delegation, escalation, and scheduling:

- `ChiefArchitectAgent` (L0)
- `ComponentLeadAgent` (L1)
- `ModuleLeadAgent` (L2)
- `TaskAgent` (L3)
- Delegation and escalation logic
- Work-stealing scheduler

Keystone retains only transport primitives:

- `MessageBus` — local intra-host routing via BlazingMQ and concurrentqueue
- `NATSListener` — NATS JetStream consumer abstraction
- `NatsConnection` — NATS client lifecycle management
- `TransparentBridge` — automatic local-to-NATS promotion for off-host destinations

Agent-to-agent communication in ProjectAgamemnon is accomplished exclusively by
publishing and subscribing to NATS subjects exposed by Keystone. ProjectAgamemnon
has no knowledge of Keystone internals; Keystone has no knowledge of agent types.

## Historical Note on ADR Numbering

CLAUDE.md originally contained the reference "per ADR-006" for this extraction
decision. ADR-006 (`ADR-006-agent-interface-type-safety-concepts.md`) documents a
separate, unrelated decision: the use of C++20 Concepts for compile-time agent
interface verification. The decoupling decision was not formally recorded in an ADR
at the time it was made. This document (ADR-015) is the authoritative record of that
architectural decision. The CLAUDE.md reference has been updated from "ADR-006" to
"ADR-015" to reflect this correction.

## Consequences

### Positive

- **Pure transport mandate**: Keystone is now exclusively a transport library. Its
  public API surface is limited to routing, subscription, and delivery primitives.
- **Independent evolution**: Transport interfaces (e.g., NATS subject schema,
  MessageBus backpressure policy) can change without affecting agent orchestration code,
  and vice versa.
- **Smaller dependency footprint**: Consumers needing only message routing do not pull
  in agent orchestration code.
- **Cleaner testing**: Transport tests exercise only routing correctness; agent tests
  exercise only orchestration correctness.
- **CLAUDE.md accuracy**: The project description now correctly reflects the
  repository's single-purpose scope.

### Negative / Trade-offs

- **Two repositories to update**: Cross-cutting changes that touch both transport
  protocol and agent behavior require coordinated PRs in both Keystone and
  ProjectAgamemnon.
- **Version coordination**: ProjectAgamemnon must pin or track a compatible version of
  Keystone's transport primitives.

### Neutral

- Existing ADR-006 (agent interface type safety via C++20 Concepts) remains valid and
  unchanged. Its content applies to agent code that now lives in ProjectAgamemnon.
- ADR-014 (INatsConnection exception contract) remains the authoritative record for
  NATS exception handling in the transport layer.
