# Fleet allocation attachment gateway

`keystone-fleet-gateway` is a C++20 transport process. An authenticated SSH/Slurm
attachment starts it inside an existing allocation. It reads commands on stdin
and writes bounded JSONL responses and separate observation frames on stdout.
It opens no listening port and creates no stream, consumer, task, or allocation.
Keep login-node commands transient. This binary does not authenticate SSH users,
submit Slurm jobs, provide an agent runtime, or make task-ownership decisions.

## Prepare and attach

1. Build with `just fleet-build`. The executable is
   `build/fleet/bin/keystone-fleet-gateway`. The normal Keystone build also includes
   and installs it.
2. Provision the canonical Keystone stream and a durable **pull** consumer using
   the existing deployment process. Require explicit acknowledgment,
   `MaxAckPending=1`, and the exact configured filter. The gateway refuses an absent
   or incompatible consumer instead of creating or changing it. Set an appropriate
   `AckWait`; the worker must send `inProgress` before it expires.
3. Give each logical agent its own consumer binding. A runtime with 24
   conversations needs 24 independently attached bindings to admit 24 outstanding
   deliveries. Consumer delivery alone is not a claim: Agamemnon still controls
   durable task claims, generation fences, and duplicate-execution prevention.
   Overlapping role filters on a limits-retention stream each receive the same
   message; this is not load balancing. WorkQueue-retention streams reject
   overlapping consumer filters. Before enabling a worker driver, provision
   admitted task/owner filters and test wrong-owner receipts and reassignment.
   The gateway does not resolve this admission policy.
4. Place scoped NATS credentials in private allocation storage. Set
   `KEYSTONE_NATS_CREDS` to a NATS credentials-file path, or set
   `KEYSTONE_NATS_TLS_CERT_PATH` and `KEYSTONE_NATS_TLS_KEY_PATH` for mutual TLS.
   Set `KEYSTONE_NATS_TLS_CA_PATH` for a private CA. The connection requires TLS
   and credentials; certificate and hostname verification remain enabled.
   Do not place secrets in arguments, workspaces, or JSON frames.
5. Through the authenticated allocation attachment, execute this command with
   actual admitted identities and the existing mesh-accessible broker endpoint:

   ```sh
   keystone-fleet-gateway \
     --stream homeric-myrmidon \
     --consumer agent-one \
     --subject 'hi.myrmidon.cpp.contributor.task.task-one' \
     --publish-prefix hi.fleet.events.worker-one \
     --worker-id worker-one --generation 7
   ```

   Set `KEYSTONE_NATS_URL` to the TLS endpoint privately, or use `--nats-url` with
   an endpoint that contains no embedded credentials. Only configured role-task
   filters or `hi.fleet.control.<worker>` are accepted. Publication is confined to
   `hi.fleet.events.<worker>` and its token-delimited descendants. Broker-side
   subject authorization must enforce the same boundaries. Existing Tailscale
   requirements remain in force; HPC attachment reachability needs its own canary.
6. Read both frame types continuously. Serialize commands on each attachment and
   use a unique `requestId` to correlate responses. Close stdin to detach. EOF
   destroys local handles **without acknowledging or deleting the durable
   consumer**. Reconnect to that same consumer and reconcile before execution.

The loopback-only `--allow-loopback-test` option permits an unauthenticated
`nats://127.0.0.1:<port>` test server. It refuses hostnames and non-loopback
addresses. It is not a production transport mode.

## JSONL interface

Every command includes `schema: "hi/keystone/fleet-attach/v1"`, `requestId`, and
`operation`. Commands and responses are at most 1 MiB excluding the terminating
newline. UTF-8 payload strings are at most 128 KiB. Payload bytes are preserved;
the gateway does not change the nested `hi/fleet/v1` command or interpret its
task, admission, or generation policy.

| Operation | Additional command fields | Successful response fields |
|---|---|---|
| `pull` | None | `empty`, or `deliveryId`, `subject`, `payload`, `streamSequence`, `numDelivered` |
| `publish` | `subject`, `payload` string, `messageId` | `streamSequence`, `duplicate` |
| `ack` | `deliveryId` | `confirmation: "broker"` |
| `nak` | `deliveryId` | `confirmation: "transport"` |
| `inProgress` | `deliveryId` | `confirmation: "transport"` |

Responses include `type: "response"`, `schema`, `requestId`, and `ok`. Failure
responses include a bounded error code. Invalid JSON does not echo input. A
missing final newline or oversized frame terminates the attachment with exit code
2 before that command executes. Clean EOF returns 0. Invalid delivered payloads
remain unacknowledged and require reconciliation; the gateway does not discard
them to advance the queue.

`pull` fetches one message and waits at most one second. No second pull is allowed
while that binding has a pending delivery. `deliveryId` is unique to an attachment
and delivery; a receipt from a previous connection cannot acknowledge a current
message. Original broker subjects and durable identities are unchanged.

`ack` uses JetStream's synchronous acknowledgment and reports success only after
the broker confirms it. `nak` and `inProgress` flush the connection; the response
states that this is transport confirmation, not a JetStream acknowledgment.
`inProgress` retains the delivery. NAK and detach leave redelivery to the broker.
No background heartbeat acknowledges work automatically.

`publish` sets `Nats-Msg-Id` and waits for the JetStream publish acknowledgment.
Retries must use the same identifier **and content**; the broker's deduplication
window is finite and does not enforce content identity. Durable command
idempotency remains the caller's responsibility. A failed publication or lost
response can have an unknown outcome; never create a new execution on that basis.

## Read-only dashboard observations

Natural transport operations emit a separate frame (formatted here for clarity;
wire frames use one line):

```json
{
  "schema": "hi/keystone/fleet-attach/v1",
  "type": "observation",
  "observation": {
    "schema": "hi/fleet/observation/v1",
    "eventId": "attachment:1",
    "sourceId": "keystone:worker:consumer:attachment",
    "sourceSequence": 1,
    "observedAt": "2026-09-10T12:00:00.000Z",
    "source": "Agamemnon",
    "target": "Hephaestus",
    "workerId": "worker",
    "generation": 7,
    "transport": "nats-jetstream",
    "subject": "hi.myrmidon.cpp.contributor.task.task-one",
    "consumerId": "agent-one",
    "stream": "homeric-myrmidon",
    "messageId": "command-one",
    "operation": "deliver",
    "result": "received",
    "bytes": 256
  }
}
```

This is a protocol example, not a recorded run. The process produces the actual
timestamp and identifiers. `sourceSequence` orders one `sourceId` only; each
attachment has a new random identity. The collector must expose gaps and use
`eventId` for deduplication. Frames are not durably retained by this process;
Argus or the attachment owner must persist them if replay is required.

Only bounded identifier fields from the top-level `hi/fleet/v1` envelope are
selected: `correlationId`, `taskId`, `executionId`, `agentId`, and `operation`
(reported as `messageKind`). `messageId` uses the broker header when present,
then the envelope's event/command ID, then the stream sequence. No nested payload,
prompt, source file, credential, or error-stack text is included.

`source` and `target` describe the configured Fleet logical route: admitted
commands flow Agamemnon → Hephaestus, worker facts flow Hephaestus → Agamemnon,
and acknowledgments flow Hephaestus → Keystone. They do not prove an authenticated
publisher identity. `operation` distinguishes `publish`, `deliver`, `redeliver`,
`ack`, `nak`, and `inProgress`; reuse the message ID to show their relationship.
`bytes` is measured payload length for publication/delivery only, never packet
size. ACK byte counts and end-to-end latency are not measured and are omitted.

The dashboard consumes these observation frames or their Argus projection. It
must never pull or acknowledge a work message to animate it. Observation frames
carry no authority to change a task. The attachment reader must drain stdout;
backpressure stops that binding without creating additional work deliveries.

## Validation and limits

1. Put `nats-server` on PATH and run `just fleet-test`. It builds at most two jobs
   concurrently and launches fresh private loopback JetStream servers in temporary
   directories. `just fleet-test-only` reruns the already-built tests.
2. Run `just fleet-format-check`. The focused recipe uses pinned clang-format
   tooling. Standard repository format/lint/sanitizer checks still apply before
   merging.
3. In a full Keystone build, opt in with
   `-DENABLE_FLEET_INTEGRATION_TESTS=ON`; the existing non-integration test gate does
   not require a local server installation.
4. Before deployment, separately prove allocation placement, SSH/Teleport
   identity, TLS/subject authorization, broker reachability, supervisor lifecycle,
   requeue recovery, and coexistence with the worker's private Unix socket.

Local tests establish real framing, explicit ACK, deduplication, in-progress
retention, NAK/redelivery, consumer independence, and detach behavior. They do not
establish cluster transport, Fleet task admission, agent performance, or the
108-agent acceptance target. This slice does not provide an automatic worker
driver, Slurm launcher, observation persistence service, or Unix-socket proxy.
