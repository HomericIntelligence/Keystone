# ADR-017: Reliable Scheduler Sleep Notifications

**Status**: Proposed
**Date**: 2026-09-14
**Deciders**: Keystone maintainers
**Tags**: architecture, concurrency, scheduler
**Related**: ADR-002

## Context

[ADR-002](ADR-002-work-stealing-scheduler-architecture.md) describes scheduler
submission and stealing as having no mutex contention. The current scheduler
uses SPIN, YIELD, and SLEEP phases. Its SLEEP phase needs reliable notifications
when a producer publishes work.

A shutdown-only condition-variable predicate can ignore a work notification
until the timeout. A queue check outside the wait mutex also leaves a gap in
which submission can notify before the worker enters the wait. Controlled
regressions exercise both cases through the same SLEEP helper that production
uses. They establish wake behavior, not host speed or scheduler throughput.

This proposal updates only the SLEEP notification and submission-locking
contract in ADR-002. The accepted ADR remains unchanged. Its historical
performance results do not qualify this implementation.

## Decision

Use the existing scheduler mutex and condition variable to coordinate SLEEP
queue checks and work notifications:

1. Both function and coroutine `submitTo` overloads enqueue work through the
   existing worker queue. They then acquire the shared scheduler mutex and
   notify waiting workers. The corresponding `submit` overloads use this path.
2. A SLEEP worker holds the same mutex while it checks for work and enters the
   condition-variable wait. The wait releases the mutex atomically. A producer
   that publishes after the empty check cannot notify across that gap.
3. Each notification, spurious wake, or timeout rechecks work unless shutdown
   has been requested. A shutdown-only predicate must not suppress work wakes.
4. The helper releases the mutex before the worker executes a function or
   resumes a coroutine. User work does not execute under this mutex.

The queue operations themselves are unchanged. End-to-end submission now takes
a mutex and can block behind another notifier or a SLEEP queue scan. The SLEEP
scan also takes this mutex. SPIN and YIELD queue scans acquire no new mutex.
This supersedes ADR-002's no-mutex claim for submission and SLEEP scans only;
it does not replace the per-worker queues with a global task queue.

The public API, worker selection, queue behavior, and CPU-affinity options
remain unchanged. The existing iteration thresholds of 100 for SPIN and 1000
for the transition from YIELD to SLEEP remain unchanged. The SLEEP wait timeout
remains one millisecond. Shutdown retains its notification, worker join, and
own-queue drain behavior. This change does not authorize new task admission or
alter the policy for submission during shutdown.

## Consequences

The shared mutex closes the empty-check-to-wait notification gap. Rechecking
after each wake allows new work to end a wait without waiting for its timeout.
The one-millisecond timeout remains a fallback; it is not a wall-clock
completion guarantee under operating-system scheduling.

Every submission now shares a notification lock, including while workers are
active. SLEEP scans can also contend with producers and each other. Contention,
throughput, scaling, and idle CPU effects have not been measured. Prior ADR-002
benchmark claims must not be attributed to this change. Independent resource
and performance runs remain necessary before making such claims.

## Alternatives considered

Keeping the shutdown-only predicate or notifying without the shared mutex
retains the demonstrated wake defects. Raising timing thresholds does not
repair those defects. Replacing the queue or adding a separate scheduler
state machine would broaden this repair. This proposal uses the existing
mutex and condition variable at the SLEEP boundary.

## Verification

- [Scheduler sleep tests](../../../tests/unit/test_scheduler_sleep.cpp) use the
  real queue and a controlled wait boundary to cover notification before and
  during a wait, repeated and spurious wakes, and shutdown.
- [Scheduler backoff tests](../../../tests/unit/test_scheduler_backoff.cpp)
  retain the queued-handoff limit of 200 microseconds, or 5 milliseconds under
  ASan or TSan. Idle-wakeup and load-latency coverage remains in the required
  serial unit selection.
- The [local CI runbook](../../runbooks/local-ci.md#evidence-boundaries)
  separates these checks from performance and Fleet acceptance measurements.

Focused test results cannot replace full CI, sanitizer, and PR-review gates.
This record remains Proposed until the repository review process accepts it.
