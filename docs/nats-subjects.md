# NATS Subject Schema and Payload Envelope

## Overview

This document is the canonical reference for all NATS subjects owned by ProjectKeystone
and the payload envelope contract that all publishers (e.g., ProjectHermes) and
subscribers (e.g., Odysseus, myrmidons) must adhere to.

**Keystone owns all NATS streams and subject schemas.** No component creates or manages
NATS streams directly.

**Last Updated**: 2026-04-22
**Document Version**: 1.0

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

All messages published to Keystone-owned NATS subjects **must** use the following
envelope structure. This contract is binding for all publishers (Hermes, Agamemnon,
Odysseus).

### Envelope Schema

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

### Task Event `data` Fields

For events on `hi.tasks.>` subjects:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `data.id` | string | yes | Unique task identifier. Same as `{taskId}` in the subject. |
| `data.teamId` | string | yes | Team owning the task. Same as `{teamId}` in the subject. |
| `data.status` | string | yes | Current task status. See [Task Status Values](#task-status-values). |
| `data.result` | any | no | Task output. Present on `completed` events. |
| `data.error` | string | no | Error description. Present on `failed` events. |

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

### Error: `status` at top level (issue #107)

**Wrong — do not do this:**

```json
{
  "event": "task.completed",
  "status": "completed",
  "data": { "id": "task-abc123", "teamId": "team1" },
  "timestamp": "2026-04-22T14:00:00Z"
}
```

**Why it fails:** Subscribers (Keystone, Odysseus) read `payload["data"]["status"]`.
A top-level `status` field will be silently ignored, causing the event to be missed.
This is the root cause of issue #107.

**Correct:** `status` is always nested inside `data`.

---

## Subscriber Contract

All subscribers reading from Keystone subjects **must** extract task status as:

```
status = payload["data"]["status"]
```

For backwards compatibility with any pre-issue-#107 publishers, subscribers **may**
fall back to `payload["status"]` only if `payload["data"]["status"]` is absent, but
must log a warning when the fallback is triggered.

```
if payload.data.status is present → use it
else if payload.status is present → use it, emit warning
else → treat as missing status
```

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
