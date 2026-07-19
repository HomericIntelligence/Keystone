# NATS Subject Schema and Payload Envelope

## Overview

This document is the canonical reference for all NATS subjects owned by Keystone
and the payload envelope contract that all publishers (e.g., ProjectHermes) and
subscribers (e.g., Odysseus, myrmidons) must adhere to.

**Keystone owns all NATS streams and subject schemas.** No component creates or manages
NATS streams directly.

**Last Updated**: 2026-06-03
**Document Version**: 1.1
**Status**: Contract applies to downstream consumers post-ADR-016; Keystone forwards verbatim

---

## Subject Registry

| Stream | Subject Pattern | Direction | Primary Consumers |
|--------|----------------|-----------|-------------------|
| `homeric-research` | `hi.research.>` | PULL | Research myrmidons |
| `homeric-myrmidon` | `hi.myrmidon.{type}.>` | PULL | Pipeline myrmidons |
| `homeric-pipeline` | `hi.pipeline.>` | PUB/SUB | Odysseus, Argus |
| `homeric-agents` | `hi.agents.>` | PUB/SUB | Argus |
| `homeric-tasks` | `hi.tasks.>` | PUB/SUB | Agamemnon, Odysseus |
| `homeric-logs` | `hi.logs.>` | PUB | Argus/Loki, Odysseus |

### Task Subject Naming Convention

```
hi.tasks.{teamId}.{taskId}.{verb}
```

| Token | Description | Example values |
|-------|-------------|----------------|
| `teamId` | Logical team owning the task | `team1`, `research` |
| `taskId` | Unique task identifier | `task-abc123` |
| `verb` | Event type | `created`, `updated`, `completed`, `failed` |

**Examples:**

- `hi.tasks.team1.task-abc123.completed`
- `hi.tasks.research.task-xyz.updated`
- `hi.tasks.pipeline.task-001.failed`

---

## Payload Envelope Contract

All messages published to Keystone-owned NATS subjects **should** use the following
envelope structure. This contract is binding for new publishers (Hermes, Agamemnon,
Odysseus).

### Canonical Envelope Schema (Hermes Format)

```json
{
  "event":     "<stream>.<verb>",
  "data":      { ... },
  "timestamp": "<ISO 8601 UTC>"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `event` | string | yes | Dot-separated event name. Matches the NATS subject verb. |
| `data` | object | yes | Event-specific payload. **All domain fields are nested here.** |
| `timestamp` | string | yes | ISO 8601 UTC timestamp of when the event was emitted. |

### Backwards Compatibility: Alternative Formats

Keystone forwards payloads verbatim and does not inspect their format; the backwards
compatibility described here is a contract that **downstream consumers** of Keystone
subjects (e.g., ProjectAgamemnon, Argus, pipeline myrmidons) must honor when reading
pre-issue-#107 messages. While new publishers **must** use the canonical Hermes format
above, consumers should accept:

- **Flat format** (deprecated): `{"status": "...", "newStatus": "...", ...}` — status at top level
- **Hermes format** (canonical): `{"event": "...", "data": {"status": "...", ...}, "timestamp": "..."}`
- **Mixed format**: Payloads with status in both locations (top-level takes precedence)

### Task Event `data` Fields (Canonical Hermes Format)

For events on `hi.tasks.>` subjects, the `data` object contains:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `data.id` | string | yes | Unique task identifier. Same as `{taskId}` in the subject. |
| `data.teamId` | string | yes | Team owning the task. Same as `{teamId}` in the subject. |
| `data.status` | string | yes | Current task status. See [Task Status Values](#task-status-values). |
| `data.result` | any | no | Task output. Present on `completed` events. |
| `data.error` | string | no | Error description. Present on `failed` events. |

**Note**: For backwards compatibility, consumers should also accept `status`, `teamId`,
and other fields at the top level when emitted by legacy publishers (see
[Backwards Compatibility](#backwards-compatibility-alternative-formats)). Keystone itself
does not parse these fields — it forwards the payload unchanged.

#### Task Status Values

| Value | Meaning |
|-------|---------|
| `pending` | Task created, not yet started. |
| `in_progress` | Task is actively running. |
| `completed` | Task finished successfully. Terminal. |
| `failed` | Task finished with an error. Terminal. |
| `cancelled` | Task was cancelled before completion. Terminal. |

### Correct Example: `task.completed`

```json
{
  "event": "task.completed",
  "data": {
    "id": "task-abc123",
    "teamId": "team1",
    "status": "completed",
    "result": { "output": "..." }
  },
  "timestamp": "2026-04-22T14:00:00Z"
}
```

### Correct Example: `task.updated` with status transition

```json
{
  "event": "task.updated",
  "data": {
    "id": "task-abc123",
    "teamId": "team1",
    "status": "failed",
    "error": "Upstream dependency timed out"
  },
  "timestamp": "2026-04-22T14:01:00Z"
}
```

---

## Common Schema Errors

### Error: Top-level `status` instead of nested (Deprecated Format — Still Supported)

**Deprecated format — do not use in new publishers:**

```json
{
  "status": "completed",
  "data": { "id": "task-abc123", "teamId": "team1" }
}
```

This was the format used before issue #107 was fixed (subscribers were reading top-level
`status`). Hermes now emits the canonical format with `status` nested inside `data`.

**Consumers should still accept this for backwards compatibility**, but new publishers
**must use the canonical format** (see [Canonical Envelope Schema](#canonical-envelope-schema-hermes-format)).
Keystone itself forwards the payload regardless of which format is used.

### Error: Missing `status` entirely

If a payload has neither `data.status` nor top-level `status`, Keystone still forwards
it on the subject (Keystone does not validate payload contents). Downstream consumers
will see no effective status for that event; this is acceptable for sparse payloads and
events like `task.updated` where the verb itself conveys the action.

---

## Subscriber Contract

This contract applies to **downstream consumers** that subscribe to Keystone-owned
NATS subjects (e.g., ProjectAgamemnon's orchestration daemon, Argus, pipeline
myrmidons). It does **not** apply to Keystone itself: Keystone is a pure byte
transport (see AGENTS.md) and does not inspect the `status` field.

Consumers reading from Keystone subjects **must** implement the following status
resolution priority (issue #107):

```
1. payload["status"]              (top-level, highest priority)
2. payload["data"]["status"]      (nested, canonical Hermes format)
3. payload["newStatus"]           (legacy fallback)
4. (none)                         (if no status found)
```

The reference implementation of this resolution now lives in ProjectAgamemnon's
`TaskEvent` model (extracted from Keystone per ADR-016).

### Deprecation Notice

The flat-format `payload["status"]` is a legacy format from before issue #107 was fixed.
All new publishers **must** use the canonical Hermes format with nested `data.status`.
This backwards compatibility layer may be removed in v0.4.0 (after a 6-month deprecation period).

---

## Payload Validation

### Hermes Publisher Validation

This document was validated against the actual Hermes publisher in ProjectHermes
`src/hermes/publisher.py` (lines 86-91). Hermes publishes task events with:

- `event` field matching the subject verb (e.g., `"task.completed"`)
- `data` object containing task ID, team ID, status, and optional `result`/`error`
- ISO 8601 UTC `timestamp`

### Keystone Transport Implementation

Keystone's NATS transport is implemented in C++20 under `src/transport/`
(BlazingMQ-backed local queue, transparent NATS bridge) and `src/network/`
(NATS connection and listener primitives). Keystone forwards messages
verbatim and does not parse payload `status` — that responsibility belongs
to downstream consumers (see [Subscriber Contract](#subscriber-contract)).
The Python `TaskEvent` model and its tests (`tests/test_task_event.py`,
`tests/test_nats_listener.py`) were removed alongside the Python orchestration
package per ADR-016 and now live in ProjectAgamemnon.

---

## Rate Limiting

Keystone enforces pull-based delivery:

- `MaxAckPending = 1` per myrmidon consumer (configurable).
- Each myrmidon calls `natsSubscription_Fetch(batch=1)` when ready.
- Durable consumers survive restarts without message loss.
- Back-pressure is automatic: a slow consumer stops fetching without dropping messages.

---

## Cross-References

- `AGENTS.md` — Architecture overview and transport design.
- `docs/MESSAGE_PROTOCOL_EXTENSIONS.md` — KIM protocol extensions.
- `docs/NETWORK_PROTOCOL.md` — gRPC/network layer protocol.
- GitHub issue #107 — NATS payload schema mismatch (status nested in `data`, not top-level).
- GitHub issue #87 — `task.failed` handling (also affected by issue #107 schema).
