# Integration Tests — NATS Transport Pipeline

This directory contains integration tests that exercise the full Keystone
transport pipeline: NATS subject delivery → MessageBus routing → agent
processing → response.

## Test categories

### Always-run tests (no NATS server required)

These tests simulate the NATS → MessageBus bridge in-process and run as part
of every CI build:

| Test | What it verifies |
|------|-----------------|
| `PipelineLocalEventTriggersAgentProcessing` | NATS-style payload reaches the target agent via MessageBus |
| `PipelineStartupScanPopulatesRegistry` | Agent registry is fully populated on startup |
| `PipelineShutdownDrainsCleanly` | All queued messages are drained before agents are deregistered |
| `PipelinePriorityMessagesDeliveredInOrder` | HIGH- and NORMAL-priority messages are both delivered |
| `PipelineCancellationPropagates` | CANCEL_TASK signal reaches the executing agent |

### NATS-server tests (requires `KEYSTONE_INTEGRATION_TESTS=1`)

These tests require a live NATS server with JetStream enabled.  They are
automatically skipped when the environment variable is not set:

| Test | What it verifies |
|------|-----------------|
| `NatsConnectionSucceeds` | TCP connectivity to the configured NATS server |
| `NatsEventTriggersPipelineAdvance` | End-to-end: NATS event → bridge → agent processes `advance_dag` |
| `NatsShutdownDrainsSubscription` | SIGTERM-style shutdown drains in-flight messages cleanly |

## Prerequisites

### Local NATS server (Docker Compose)

```bash
# Start NATS with JetStream enabled
docker-compose -f docker-compose.test.yml up -d nats

# Verify the server is healthy
curl -s http://localhost:8222/healthz
```

### Bare-metal (nats-server binary)

```bash
# macOS
brew install nats-server

# Linux
curl -L https://github.com/nats-io/nats-server/releases/latest/download/nats-server-linux-amd64.zip | bsdtar -xf -
./nats-server --jetstream
```

## Running the tests

```bash
# Always-run tests only (no NATS required)
just test

# Full integration suite (requires NATS server)
just integration-test

# Run with a custom NATS URL
NATS_URL=nats://myserver:4222 just integration-test

# Run inside Docker Compose (NATS + tests)
docker-compose -f docker-compose.test.yml up --abort-on-container-exit
```

## CI integration

The CI pipeline runs always-run tests on every PR.  NATS-dependent tests run
in a separate job that uses the `nats:2.10-alpine` service container defined
in `docker-compose.test.yml`.

Environment variables used by the test suite:

| Variable | Default | Purpose |
|----------|---------|---------|
| `KEYSTONE_INTEGRATION_TESTS` | _(unset)_ | Set to `1` to enable NATS-dependent tests |
| `NATS_URL` | `nats://localhost:4222` | NATS server URL |

## NATS subject schema

The integration tests follow the ProjectKeystone subject schema:

| Subject | Direction | Tested by |
|---------|-----------|-----------|
| `hi.tasks.execute` | PULL | `NatsEventTriggersPipelineAdvance` |
| `hi.agents.shutdown` | PUB/SUB | `NatsShutdownDrainsSubscription` |
| `hi.myrmidon.{type}.>` | PULL | `PipelineLocalEventTriggersAgentProcessing` |

## Troubleshooting

**Tests skip even with `KEYSTONE_INTEGRATION_TESTS=1`**
Make sure the NATS server is running before executing the tests.  The
`NatsConnectionSucceeds` test will report a clear error if the server is
unreachable.

**`NatsConnectionSucceeds` fails**
Check that the NATS server started correctly:

```bash
docker-compose -f docker-compose.test.yml logs nats
curl http://localhost:8222/healthz
```

**JetStream not available**
Ensure the NATS server was started with `--jetstream`.  The Docker Compose
service in `docker-compose.test.yml` enables it by default.
