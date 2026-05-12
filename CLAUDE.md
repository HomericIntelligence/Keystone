# ProjectKeystone — Pure Invisible Transport Layer

## Project Overview

**ProjectKeystone** is a C++20 transport infrastructure library providing invisible,
zero-configuration message routing for all HomericIntelligence components.
Components never address Keystone directly — they publish and subscribe to logical
NATS subjects, and Keystone handles all routing transparently.

Keystone is **not** an agent system, pipeline stage, or orchestrator. It is the
invisible plumbing beneath every other component.

> **Note**: The 4-layer HMAS hierarchy (L0 ChiefArchitectAgent → L1 ComponentLeadAgent →
> L2 ModuleLeadAgent → L3 TaskAgent) was extracted from this repository and merged into
> **ProjectAgamemnon** per ADR-015 (full decoupling from ai-maestro). Keystone retains
> only transport primitives.

---

## Language and Technology Stack

### Primary Language: C++20

**The transport runtime in this project is EXCLUSIVELY C++20. Do NOT use
Python, Mojo, or other languages for new transport, message-bus, or
agent-runtime code.**

**Exception — supporting Python tooling.** A small number of Python modules
remain in `src/keystone/` as a thin orchestration / test harness layer
(`config.py`, `daemon.py`, `dag_walker.py`, `models.py`, `nats_listener.py`,
`task_claimer.py`, `validation.py`, `logging.py`). These predate the ADR-015
extraction to ProjectAgamemnon and are still imported by the Python tests
under `tests/`. They are maintained in-place but **must not** grow new
production responsibilities — any new orchestration logic belongs in
ProjectAgamemnon, and any new transport logic must be implemented in C++20
under `src/transport/`, `src/network/`, or `include/`.

### Required Technologies

| Technology | Version | Purpose |
|-----------|---------|---------|
| C++20 | — | Implementation language |
| CMake | 3.20+ | Build system |
| CMakePresets.json | v8 | Preset-based build configuration |
| BlazingMQ | latest | Local queue management |
| concurrentqueue | latest | Lock-free intra-host message queuing |
| nats.c | 3.12.0 | NATS JetStream client (cross-host) |
| GoogleTest | latest | Unit and integration testing |
| clang-format | — | Code formatting (warnings-as-errors) |
| clang-tidy | — | Static analysis (warnings-as-errors) |
| Sanitizers | — | ASan, UBSan, TSan runtime checks |

---

## Architecture

Keystone provides two complementary transports that are bridged transparently:

### Local Transport (intra-host)

- **BlazingMQ** manages durable local queues.
- **C++20 MessageBus** wraps BlazingMQ and exposes a lock-free concurrent queue API
  (`concurrentqueue`).
- Backpressure and KIM (Keystone Interchange Message) protocol routing are enforced
  at the MessageBus layer.
- Target: **<500 ns routing latency**, **>2 M msg/sec throughput** on a single host.

### Cross-Host Transport

- **NATS JetStream** via `nats.c v3.12.0` provides durable, at-least-once delivery
  across hosts.
- All cross-host traffic runs over the **Tailscale WireGuard mesh**
  (`tail8906b5.ts.net`).
- The **Transparent Bridge** automatically promotes messages destined for off-host
  agents from the local MessageBus onto the appropriate NATS subject, and vice versa.
  No component needs awareness of whether its peer is local or remote.

### TaskClaimer and DAG Advancement

- **TaskClaimer** (in ProjectAgamemnon) polls the `homeric-tasks` stream for ready
  tasks pulled from Agamemnon's task queue.
- It advances the DAG via `advance_dag_tracked()` after task execution, updating
  dependency state and enabling downstream task readiness.

### Transport Decision Flow

```
Publisher
   │
   ▼
MessageBus.route(msg)
   │
   ├─ destination is local agent → deliver via lock-free queue
   │
   └─ destination is off-host    → publish to NATS subject via bridge
                                        │
                                        ▼
                                   NATS JetStream
                                        │
                                        ▼
                                   Remote host MessageBus
                                        │
                                        ▼
                                   Remote subscriber
```

---

## NATS Subject Schema

All logical streams are owned and routed by Keystone. No component creates or
manages NATS streams directly.

| Stream | Subject Pattern | Direction | Primary Consumers |
|--------|----------------|-----------|-------------------|
| `homeric-research` | `hi.research.>` | PULL | Research myrmidons |
| `homeric-myrmidon` | `hi.myrmidon.{type}.>` | PULL | Pipeline myrmidons |
| `homeric-pipeline` | `hi.pipeline.>` | PUB/SUB | Odysseus, Argus |
| `homeric-agents` | `hi.agents.>` | PUB/SUB | Argus |
| `homeric-tasks` | `hi.tasks.>` | PUB/SUB | Agamemnon, Odysseus, TaskClaimer |
| `homeric-logs` | `hi.logs.>` | PUB | Argus/Loki, Odysseus |

---

## Rate Limiting

Keystone enforces pull-based, rate-limited delivery to prevent myrmidon overload:

- `MaxAckPending = 1` per myrmidon (configurable per consumer).
- Pull loop calls `natsSubscription_Fetch(batch=1)` — each myrmidon pulls exactly
  one task when it is ready for more work.
- Durable consumers per myrmidon — consumer state survives restarts and reconnects.
- Back-pressure is automatic: a slow myrmidon simply stops fetching, and the message
  accumulates in the JetStream subject without being dropped.

---

## What Was Moved to ProjectAgamemnon (ADR-006, ADR-015)

### C++ Agent Layer (ADR-015)

The following are **no longer part of Keystone**. They live in ProjectAgamemnon:

- L0 `ChiefArchitectAgent`
- L1 `ComponentLeadAgent`
- L2 `ModuleLeadAgent`
- L3 `TaskAgent`
- Delegation and escalation logic
- Work-stealing scheduler
- HMAS 4-layer hierarchy design

### Python Orchestration Layer (ADR-015, 2026)

The following Python modules were also extracted from Keystone into ProjectAgamemnon
as part of the ADR-015 follow-up (HomericIntelligence/Odysseus#143):

- `dag_walker.py` — DAGWalker (task ready-set computation, cycle detection)
- `task_claimer.py` — TaskClaimer (per-team concurrency guard, drain)
- `nats_listener.py` — NATSListener (NATS subject subscriber, `hi.tasks.>`)
- `maestro_client.py` — MaestroClient (AI Maestro REST client)
- `models.py` — Task, Agent, TaskEvent domain models
- `daemon.py` — orchestration daemon entry point
- `config.py`, `logging.py`, `validation.py` — supporting utilities

These modules are now part of `agamemnon.orchestration` in ProjectAgamemnon.
Keystone retains sole ownership of the NATS subject schema but no longer runs
a Python subscriber.

---

Keystone's only concern is moving bytes from publisher to subscriber, reliably and
transparently.

---

## Build System

### CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(ProjectKeystone CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

### CMakePresets.json

Use preset-based builds (v8 schema). Common presets:

```bash
cmake --preset debug          # Debug build
cmake --preset release        # Release build
cmake --preset asan           # Debug + AddressSanitizer
cmake --preset tsan           # Debug + ThreadSanitizer
cmake --preset ubsan          # Debug + UndefinedBehaviorSanitizer
cmake --build --preset asan
ctest --preset asan
```

### Public Dependency Visibility

**IMPORTANT**: `spdlog` and `concurrentqueue` must be linked as `PUBLIC` (not `PRIVATE`)
on `keystone_core` and `keystone_concurrency` targets because their headers are exposed
in the public API via `logger.hpp` and message queue types. Downstream consumers need
transitive access to these headers at compile time.

### Build Output Structure

```
build/
├── x86/          # Default release build (CMakePresets.json default)
├── x86.debug/    # Debug build variant
├── debug/        # Debug build (alternative preset)
├── tsan/         # Debug + ThreadSanitizer
├── ProjectMnemosyne/  # Legacy/archived build directory
├── conan-deps/   # Conan dependency cache
└── reports/      # Build reports and test results
```

### Test Structure

```
tests/
├── unit/           # Unit tests per component
├── integration/    # Cross-component integration tests
├── e2e/            # End-to-end and distributed tests
├── load/           # Load and throughput tests
├── fixtures/       # Shared test fixtures
├── mocks/          # Mock implementations
├── conftest.py     # pytest configuration and fixtures
├── helpers.py      # Shared test helper utilities
├── __init__.py     # Python package marker
└── test_*.py       # Python integration and daemon tests
```

---

## Quality Tooling

### clang-format

All C++ source must pass `clang-format` with the project `.clang-format` config.
Formatting violations are treated as errors in CI.

```bash
just format        # Format all source files in-place
just format-check  # Check formatting without modifying (CI gate)
```

### clang-tidy

Static analysis is enforced via `clang-tidy` with the project `.clang-tidy` config.
All warnings are treated as errors.

```bash
just lint          # Run clang-tidy on all targets
```

### Sanitizers

| Sanitizer | Purpose |
|-----------|---------|
| ASan | Use-after-free, buffer overflows, heap corruption |
| UBSan | Undefined behavior (signed overflow, null deref, etc.) |
| TSan | Data races, lock-order inversions |

Run sanitizer builds before every PR merge:

```bash
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

### GoogleTest

Unit tests and integration tests are written using GoogleTest. All tests must pass
under sanitizer builds before merge.

```bash
ctest --preset asan --output-on-failure
ctest --preset tsan --output-on-failure
```

---

## Common Commands (justfile)

```bash
just build           # Compile default (debug) preset
just build-release   # Compile release preset
just test            # Run all tests (debug)
just test-asan       # Run all tests under AddressSanitizer
just test-tsan       # Run all tests under ThreadSanitizer
just format          # Format source in-place
just format-check    # Verify formatting (CI)
just lint            # Run clang-tidy
just clean           # Remove build directories
```

---

## Key Principles

1. **Invisible transport**: Components are never aware of Keystone. They only know
   NATS subject names. Keystone handles everything else.

2. **Transparent bridging**: Local MessageBus and NATS JetStream are bridged
   automatically. A publisher does not choose which transport to use.

3. **Pull-based, rate-limited**: Myrmidons pull at their own pace. Keystone never
   pushes more work than a consumer can handle.

4. **Durable delivery**: NATS JetStream consumers are durable. Messages survive
   consumer restarts without loss.

5. **C++20 only**: No Python, Mojo, gRPC, or other technologies for implementation.
   All transport primitives are C++20 with BlazingMQ and nats.c.

6. **Tailscale required**: All cross-host NATS traffic runs inside the Tailscale
   WireGuard mesh. No direct public IP routing.

---

## Git Workflow

### Branch-First Development

**NEVER commit directly to `main`.**

```bash
# Create a feature branch
git checkout -b feat/short-description

# For fixes
git checkout -b fix/issue-description

# For docs
git checkout -b docs/description
```

### Commit Message Format (Conventional Commits)

- `feat:` — New capability
- `fix:` — Bug fix
- `refactor:` — Internal restructure without behaviour change
- `test:` — Test additions or changes
- `docs:` — Documentation only
- `chore:` — Maintenance (deps, CI, build)

### Pull Request Checklist

Before opening a PR:

1. On a feature branch: `git branch --show-current`
2. All tests pass under ASan: `cmake --preset asan && ctest --preset asan`
3. Formatting clean: `just format-check`
4. clang-tidy clean: `just lint`
5. No compilation warnings (`-Werror` is enforced)
6. CHANGELOG.md updated for user-facing changes

---

**Last Updated**: 2026-04-25
**Version**: 3.0 (Pure Transport — HMAS and Python orchestration extracted to ProjectAgamemnon per ADR-015)
**Project**: ProjectKeystone
