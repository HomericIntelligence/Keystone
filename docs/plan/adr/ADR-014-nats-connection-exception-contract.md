# ADR-014: INatsConnection Exception Contract

**Status**: Accepted
**Date**: 2026-04-24
**Deciders**: ProjectKeystone Development Team
**Tags**: architecture, error-handling, nats, transport

## Context

ProjectKeystone uses NATS JetStream for cross-host message delivery. The
`INatsConnection` interface abstracts the nats.c client library and serves as the
foundation for all NATS operations, including stream subscription, message
publishing, and consumer management.

Error classification in downstream components (especially `NatsListener`) depends
critically on distinguishing between three categories of NATS errors:

1. **Configuration errors** (stream not found): Require administrative intervention
2. **Transient errors** (temporary connectivity loss): Justify retry with backoff
3. **Permanent errors** (authentication failure, invalid credentials): Cannot be recovered by retry

This contract is currently documented only in inline code comments scattered across
implementations, making it invisible to future maintainers and implementors of
alternative `INatsConnection` variants (e.g., TLS-wrapped connections, mock
implementations for integration testing).

**Related Issue**: #334, #139

## Problem Statement

Without a formal, explicit exception contract for `INatsConnection`:

- **Fragile implementations**: Future TLS-wrapped or mock variants may use different
  exception types, breaking error classification in `NatsListener`.
- **Silent failures**: Configuration errors (e.g., missing stream) may be misclassified
  as transient, causing infinite retries that should fail fast.
- **Difficult debugging**: Implementors have no clear guidance on which exception to
  throw for each error category.
- **Maintenance burden**: Contract is embedded in code comments rather than
  formally specified.

## Decision

Establish and document a formal exception contract for `INatsConnection`:

### Exception Hierarchy

All public `INatsConnection` methods that may fail throw one of three standard C++
exception types to signal the error category:

| Exception Type | Semantics | Recovery | Examples |
|---|---|---|---|
| **`std::domain_error`** | Configuration error requiring administrative intervention | Fail fast, require manual fix | Stream not found, invalid consumer name, incompatible stream configuration |
| **`std::system_error`** | Transient, potentially retryable error | Retry with backoff | NATS server temporarily unreachable, connection timeout, temporary service degradation |
| **`std::runtime_error`** | Permanent, non-retryable error | Fail fast | Authentication failure, permission denied, invalid credentials, exhausted max reconnects |

### API Boundaries That Enforce This Contract

All of the following methods in `INatsConnection` (or its concrete implementations)
must throw only exceptions from the above table:

- **Constructor**: May throw `std::domain_error` (bad configuration) or
  `std::system_error` (initial connection setup failure).
- **`connect()`**: May throw `std::system_error` (transient failure) or
  `std::runtime_error` (permanent failure like auth timeout).
- **`subscribe()` / `publish()`**: May throw `std::domain_error` (stream not found),
  `std::system_error` (network error), or `std::runtime_error` (permission denied).
- **`fetchMessage()`**: May throw `std::system_error` (timeout) or
  `std::runtime_error` (subscription lost).

### Rationale for Choice

1. **`std::domain_error`**: Represents violation of preconditions (stream must exist).
   Aligns with C++ standard library convention for bad input.

2. **`std::system_error`**: Wraps transient OS/network conditions. May contain
   `errno` or NATS-specific error codes. Indicates retry is worth attempting.

3. **`std::runtime_error`**: Represents logic errors or permanent resource
   exhaustion that cannot be recovered by retry alone.

## Consequences

### Positive

1. **Clear contract**: Future implementors (TLS-wrapped, mock) have explicit
   guidance on exception semantics.

2. **Robust error classification**: `NatsListener` and other consumers can classify
   errors by exception type without needing NATS-specific error codes.

3. **Testability**: Mock implementations can throw the appropriate exception type
   without needing full nats.c integration.

4. **Maintainability**: Exception semantics are centrally documented rather than
   scattered across implementation files.

5. **Graceful degradation**: Startup failures can distinguish between "fix your
   configuration" and "wait and retry later".

### Negative

1. **Broad exception types**: `std::runtime_error` is widely used; implementors
   must be disciplined about reserving it only for permanent errors. Consider
   documenting prohibited uses.

2. **No NATS-specific details**: The contract does not mandate what information is
   carried in the exception message. Implementors should include NATS error codes
   in the message for debugging (e.g., "stream not found (NATS code 10039)").

3. **Catch-by-type only**: Consumers cannot distinguish between different types of
   `std::domain_error` without parsing the error message. For critical paths,
   consider custom exception subclasses (deferred to future ADR if needed).

## Implementation Guidelines

### For INatsConnection Implementors

When wrapping nats.c or implementing an alternative transport:

```cpp
// Example: stream not found
if (nats_status == NATS_STREAM_NOT_FOUND) {
    throw std::domain_error("NATS stream not found: " + stream_name);
}

// Example: connection timeout
if (nats_status == NATS_TIMEOUT) {
    throw std::system_error(
        std::error_code(ETIMEDOUT, std::system_category()),
        "NATS connection timeout"
    );
}

// Example: authentication failure
if (nats_status == NATS_AUTH_REQUIRED) {
    throw std::runtime_error("NATS authentication failed");
}
```

### For Error Consumers (NatsListener, etc.)

When handling NATS errors:

```cpp
try {
    conn->subscribe(stream, subject, consumer_name);
} catch (const std::domain_error& e) {
    // Configuration error — fail fast
    spdlog::error("NATS configuration error: {}", e.what());
    exit(1);  // or return error status requiring manual intervention
} catch (const std::system_error& e) {
    // Transient error — retry with backoff
    spdlog::warn("NATS transient error: {}", e.what());
    return RetryableError;
} catch (const std::runtime_error& e) {
    // Permanent error — fail fast
    spdlog::error("NATS permanent error: {}", e.what());
    return FatalError;
}
```

## Testing Implications

### Unit Tests

Mock implementations should throw the appropriate exception type:

```cpp
class MockNatsConnection : public INatsConnection {
public:
    void subscribe(...) override {
        if (should_fail_config_) {
            throw std::domain_error("Mock: stream not found");
        } else if (should_fail_transient_) {
            throw std::system_error(
                std::error_code(EAGAIN, std::system_category()),
                "Mock: transient failure"
            );
        } else if (should_fail_permanent_) {
            throw std::runtime_error("Mock: permanent failure");
        }
        // ... success path
    }
};
```

### Integration Tests

Integration tests should verify that each error category is classified correctly
by the consumer:

```cpp
TEST(NatsListenerTest, ConfigurationErrorFailsFast) {
    auto mock_conn = std::make_shared<MockNatsConnection>();
    mock_conn->make_subscribe_throw_domain_error();

    NatsListener listener(config, callback);
    // Verify that listener.start() propagates or handles domain_error appropriately
}

TEST(NatsListenerTest, TransientErrorIsRetried) {
    auto mock_conn = std::make_shared<MockNatsConnection>();
    mock_conn->make_subscribe_throw_system_error();

    // Verify retry logic engages
}
```

## Related ADRs

- [ADR-001: MessageBus Architecture](ADR-001-message-bus-architecture.md) — Documents
  the synchronous routing layer that may forward NATS messages.
- [ADR-013: Coroutine Safety Patterns](ADR-013-coroutine-safety-patterns.md) — If
  NATS operations are wrapped in async functions, exception handling must follow
  the patterns established there.

## Related Issues

- **#334**: This ADR (requested documentation)
- **#139**: [Improvement] Add error handling for NATS JetStream stream-not-found
  during startup — directly uses this contract for error classification
- **#86**: NatsListener implementation that depends on this contract

## Future Work

1. **Custom exception subclasses**: If finer-grained error classification is needed
   (e.g., distinguishing between "stream not found" and "consumer not found"), consider
   creating custom exception types inheriting from the standard types:

   ```cpp
   class StreamNotFoundError : public std::domain_error { /* ... */ };
   class PermissionDeniedError : public std::runtime_error { /* ... */ };
   ```

   This would require a follow-up ADR.

2. **Error code propagation**: Standardize how NATS error codes (from nats.c) are
   included in exception messages or carried as `errno` in `std::system_error`.

3. **TLS-wrapped variant**: When implementing `TlsNatsConnection` or other variants,
   ensure they strictly follow this contract.

## Validation

### Checklist for INatsConnection Implementations

- [ ] All public methods document which exceptions they may throw
- [ ] `std::domain_error` is thrown for configuration errors (stream not found, invalid config)
- [ ] `std::system_error` is thrown for transient errors with appropriate `error_code`
- [ ] `std::runtime_error` is thrown for permanent errors (auth failure, resource exhaustion)
- [ ] Error messages include NATS-specific details (error code, stream/consumer name)
- [ ] Unit tests verify exception types for each error category
- [ ] Integration tests verify consumer error classification (NatsListener)

### Checklist for Error Consumers

- [ ] Catch `std::domain_error` and fail fast (require manual intervention)
- [ ] Catch `std::system_error` and retry with backoff
- [ ] Catch `std::runtime_error` and fail fast
- [ ] Error messages logged include enough context to debug (stream, consumer, error code)
- [ ] Tests verify retry behavior only engages for transient errors

## References

- [cppreference: std::domain_error](https://en.cppreference.com/w/cpp/error/domain_error)
- [cppreference: std::system_error](https://en.cppreference.com/w/cpp/error/system_error)
- [cppreference: std::runtime_error](https://en.cppreference.com/w/cpp/error/runtime_error)
- NATS C Client Documentation: https://github.com/nats-io/nats.c
- ProjectKeystone CLAUDE.md: NATS Subject Schema and Rate Limiting sections

## Revision History

| Date | Status | Changes |
|------|--------|---------|
| 2026-04-24 | Accepted | Initial version — INatsConnection exception contract specification, implementation guidelines, testing implications |

---

**Last Updated**: 2026-04-24
**Status**: Accepted
**Supersedes**: None
**Superseded By**: None
