# NATS Subject Schema and Payload Envelope

## Overview

This document is the canonical reference for all NATS subjects owned by ProjectKeystone
and the payload envelope contract that all publishers (e.g., ProjectHermes) and
subscribers (e.g., Odysseus, myrmidons) must adhere to.

**Keystone owns all NATS streams and subject schemas.** No component creates or manages
NATS streams directly.

**Last Updated**: 2026-04-25
**Document Version**: 1.1
**Status**: Validated against Keystone implementation (issue #252)

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

Keystone's subscriber code accepts multiple payload formats for backwards compatibility
with pre-issue-#107 publishers. While new publishers **must** use the canonical Hermes
format above, Keystone will accept:

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

**Note**: Keystone also accepts `status`, `teamId`, and other fields at the top level for
backwards compatibility with legacy publishers (see [Backwards Compatibility](#backwards-compatibility-alternative-formats)).

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

This was the format used before issue #107 was fixed (Keystone was reading top-level `status`).
Hermes now emits the canonical format with `status` nested inside `data`.

**Keystone currently accepts this for backwards compatibility**, but new publishers
**must use the canonical format** (see [Canonical Envelope Schema](#canonical-envelope-schema-hermes-format)).

### Error: Missing `status` entirely

If a payload has neither `data.status` nor top-level `status`, Keystone will still dispatch
the event but with no effective status. This is acceptable for sparse payloads and events
like `task.updated` where the verb itself conveys the action.

---

## Subscriber Contract

All subscribers reading from Keystone subjects **must** implement the following
status resolution priority (issue #107):

```
1. payload["status"] (top-level, highest priority)
2. payload["data"]["status"] (nested, canonical format)
3. payload["newStatus"] (fallback for alternative formats)
4. None (if no status found)
```

Keystone's C++ transport layer implements this exact resolution strategy via the
KIM protocol routing in `src/core/` and `src/network/`.

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

### Keystone Subscriber Validation

Keystone's NATS listener is implemented in C++ under `src/network/`. The transport
layer validates incoming payloads and routes them per the 3-way status resolution
priority listed above. Compliance is verified by the C++ integration tests under
`tests/integration/`.

---

## Rate Limiting

Keystone enforces pull-based delivery:

- `MaxAckPending = 1` per myrmidon consumer (configurable).
- Each myrmidon calls `natsSubscription_Fetch(batch=1)` when ready.
- Durable consumers survive restarts without message loss.
- Back-pressure is automatic: a slow consumer stops fetching without dropping messages.

---

## Cross-References

- `CLAUDE.md` — Architecture overview and transport design.
- `docs/MESSAGE_PROTOCOL_EXTENSIONS.md` — KIM protocol extensions.
- `docs/NETWORK_PROTOCOL.md` — gRPC/network layer protocol.
- GitHub issue #107 — NATS payload schema mismatch (status nested in `data`, not top-level).
- GitHub issue #87 — `task.failed` handling (also affected by issue #107 schema).
