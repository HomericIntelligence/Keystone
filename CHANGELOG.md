# Changelog

All notable changes to Keystone are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Starting from v0.2.0, this file is maintained automatically by
[release-please](https://github.com/googleapis/release-please).

---

## [Unreleased]

### Changed

- `build`: migrate the build toolchain from pixi/conda to uv (Odysseus ADR-018,
  mirroring Nestor #133 and Agamemnon #457). CMake, Ninja, Conan, gcovr, and
  pre-commit are now uv-managed locked PyPI wheels (`pyproject.toml` + `uv.lock`
  + `.python-version`); the C++ compiler, clang-format/clang-tidy, lcov, and the
  OpenSSL dev headers come from the system (apt). gtest continues to be provided
  by Conan (`conanfile.py`). CI swaps `prefix-dev/setup-pixi` for
  `astral-sh/setup-uv` and the `pixi-check` job becomes `uv-check`; every
  required check-run name (`lint`, `unit-tests`, `integration-tests`, `build`,
  `test`, `package`, `install`, `coverage`, `release`, `schema-validation`,
  `deps/version-sync`, `security/*`) is preserved, and the canonical `release`
  gate + `publish-release` naming are untouched (no release-check collision).
  The Containerfile builder pulls uv via a pinned `COPY --from` named stage.

### Added

- Integration test `SchedulerSigtermTest` verifying SIGTERM mid-flight causes graceful drain: all submitted tasks
complete before worker threads exit (#303)
- Migrate dependency management to Conan 2 with CMakePresets ([220326c](../../commit/220326c))
- NATS subject schema documentation and payload envelope contract
- Pre-commit CI job enforcing all hook checks
- Dependency vulnerability scanning in CI
- Pytest-cov coverage reporting in CI
- Mypy type checking in CI pipeline
- `TASK_PHASE_ERROR` variant with centralized terminal state checks
- Structured JSON logging for log aggregation
- `/v1/health` monitoring endpoint with NATS status tracking
- NATS reconnection callbacks and error handling
- `NATSListener` with explicit JetStream ack/nak on every code path
- `TASK_FAILED` event handling to prevent DAG deadlocks
- `test_task_claimer.py` and shared `tests/helpers.py` test utilities
- `justfile` wrapping Makefile for ecosystem convention
- Add CODE_OF_CONDUCT.md ([b9d15bc](../../commit/b9d15bc))
- Rewrite CLAUDE.md for pure transport role, remove HMAS hierarchy ([f442d1a](../../commit/f442d1a))

### Changed

- Remove duplicate clang-format step from lint job; pre-commit hook (mirrors-clang-format v18.1.0) is the single source
of truth for C++ formatting checks
- License file updated to BSD 3-Clause (replacing MIT placeholder)
- Audit remediation: corrected the remaining MIT license references (README badge and license section,
`CPACK_RPM_PACKAGE_LICENSE`, `docs/PACKAGING.md` example) to BSD-3-Clause so advertised licensing matches the
actual `LICENSE` file (#504)
- Audit remediation: `Containerfile` test stage now ships the transport/concurrency smoke tests
(`transport_unit_tests`, `bridge_unit_tests`, `concurrency_unit_tests`) instead of the removed agent
delegation/coordination e2e binaries — the agent hierarchy and Python orchestration were already extracted to
ProjectAgamemnon (ADR-015/016, #578) (#504)
- Unsized integers converted to sized types (`int32_t`, `uint32_t`, `size_t`)
- `CONTRIBUTING.md` rewritten to match current C++20/Conan/just workflow
- `pixi.toml` and `justfile` aligned with ecosystem conventions
- Address audit quick wins — security, templates, DX improvements ([6fbb180](../../commit/6fbb180))
- `ci`: Bump github-actions group across 1 directory with 3 updates ([c916699](../../commit/c916699))

### Fixed

- `monitoring`: Add `.load()` for atomic `server_fd_` and `port_` usages ([4a583ed](../../commit/4a583ed))
- Resolve CI failures — TSan data races, MSan removal, Dockerfile COPY paths ([412b73c](../../commit/412b73c))
- `ci`: Apply clang-format-18 and fix Dockerfile COPY for disabled tests ([14e9e42](../../commit/14e9e42))
- DAG agent availability filtering to check current task assignment
- DAG-walker recursive DFS replaced with iterative cycle detection
- CI: Conan 2 wired into CI, Makefile, and Dockerfile
- CI: security-scan.yml rewritten for C++20/pixi project
- CI: `pull-requests: write` permission for PR summary comments
- TSan data races in `SimulatedNUMANode`; `ConcurrentQueue` false positives suppressed
- `ThreadPoolTest.CreateAndDestroy` disabled under sanitizer builds
- `spdlog` and `concurrentqueue` promoted to `PUBLIC` in CMake targets
- Atomic `server_fd_` and `port_` accesses use `.load()`

---

## [0.1.0] — 2026-03-15

Initial tracked release. Establishes the C++20 transport infrastructure for
ProjectKeystone: intra-host MessageBus (lock-free), NATS JetStream cross-host
bridge, BlazingMQ integration, and full sanitizer CI.

### Added

- `core`: Add `AgentLevel` enum type (Phase 0) ([9906d95](../../commit/9906d95))
- Enhance build system with new feature flag naming convention ([39dd58d](../../commit/39dd58d))
- Add sanitizer and build type pattern rules to Makefile ([c690c77](../../commit/c690c77))
- Add comprehensive Makefile with suffix-matching for build system ([ab7d5d4](../../commit/ab7d5d4))
- Add CPack packaging with 5 component packages ([9669576](../../commit/9669576))
- Add strategy pattern for message processing (ARCH-007) ([45862a5](../../commit/45862a5))
- Add native pixi builds and fix GCC 13+ compatibility ([da6555f](../../commit/da6555f))
- Add justfile build system and multi-process agent execution example ([c1b84e1](../../commit/c1b84e1))
- Implement actual work stealing for NUMA simulation (Phase 4.2) ([f33e5b7](../../commit/f33e5b7))
- Add task cancellation notification via MessageBus (Issue #52) ([f8bb089](../../commit/f8bb089))
- Add TLS support for gRPC via environment variables (Issue #52) ([bcfc6e0](../../commit/bcfc6e0))
- Re-enable benchmark suite with unified async API (Issue #54) ([3e396faa](../../commit/3e396faa))
- Stream C Phases C2 & C3 — Agent unit tests + registry integration (Issue #45) ([591288d](../../commit/591288d))
- Stream C Phase C1 — 3-Phase scheduler backoff (Issue #42) ([2a74001](../../commit/2a74001))
- Stream B Phase B1 — `CoordinationState` template (Issue #44) ([156bf77](../../commit/156bf77))
- Stream A Phase A2 — `AgentIdInterning` registry optimization (Issue #43) ([229f40a](../../commit/229f40a))
- Stream A Phase A1 — MessageBus interface segregation (Issue #46) ([2929bc0](../../commit/2929bc0))
- Add GitHub issue workflow to all agent configurations ([5525693](../../commit/5525693))
- C++20 `MessageBus` with lock-free concurrent queue (`concurrentqueue`)
- NATS JetStream integration via `nats.c` for cross-host delivery
- Transparent bridge between local `MessageBus` and NATS JetStream
- Pull-based, rate-limited delivery (`MaxAckPending = 1` per consumer)
- Durable JetStream consumers surviving restarts
- BlazingMQ local queue management
- `WorkStealingScheduler` with async agent coroutine support
- 4-layer HMAS hierarchy (`L0`–`L3`) — subsequently extracted to ProjectAgamemnon (ADR-006)
- `spdlog`-based `Logger` and `LogContext` for distributed logging
- CMake 3.20+ build system with `CMakePresets.json` (v8)
- ASan, TSan, and UBSan sanitizer presets
- GoogleTest unit and integration test suite
- GitHub Actions CI/CD workflows
- Docker-based build environment
- HTTP monitoring server
- Python DAG orchestration daemon (`daemon.py`) for ai-maestro integration
- NATS listener with JetStream ack/nak support
- `CODE_OF_CONDUCT.md` and `SECURITY.md`
- Conan 2 dependency management with `conanfile.py`
- Add ADR-012 documenting CPack build system decisions ([f3b2769](../../commit/f3b2769))
- Add ADR-013 coroutine safety patterns (Issue #56) ([895e1a4](../../commit/895e1a4))
- Update README.md and fix Docker configuration ([9c67473](../../commit/9c67473))
- Update test framework references from Catch2 v3 to Google Test ([0bcaa5d](../../commit/0bcaa5d))
- Add comprehensive workflow examples for thread/IPC/network deployment ([2d382db](../../commit/2d382db))
- Merge implementation summaries into architecture documentation ([bb47483](../../commit/bb47483))
- Add comprehensive work stealing tests (Issue #55 Task 4.2) ([fdd94e1](../../commit/fdd94e1))

### Changed

- Migrated from Python prototype to C++20 implementation
- Migrated dependency management from vcpkg/system packages to Conan 2
- `concurrency`: Convert logger to `int32_t` worker IDs ([5f9f1e2](../../commit/5f9f1e2))
- Migrate from justfile to Makefile and consolidate CI/CD ([29713a7](../../commit/29713a7))
- Simplify Makefile patterns by removing redundant combined rules ([63e390a](../../commit/63e390a))
- Replace individual `.native` targets with pattern rule ([20fcf09](../../commit/20fcf09))
- Rename agent base classes for clarity (ARCH-001) ([16987b7](../../commit/16987b7))
- Implement Template Method Pattern for Lead Agents (Phase 4.1) ([691eb3c](../../commit/691eb3c))
- P2-06 string allocation profiling and analysis (Phase 1 complete) ([9476453](../../commit/9476453))

### Fixed

- Switch to sized integers (multiple commits, Dec 2025) ([2aeb9e4](../../commit/2aeb9e4))
- Get CMake version to work on PureOS 9 ([e775bde](../../commit/e775bde))
- Cleanup the build directory structure to reduce root spam ([3d1158f](../../commit/3d1158f))
- Fix Docker setup: environment variables and file integration ([628e832](../../commit/628e832))
- Address 9 critical security vulnerabilities (PR #1) ([def6e21](../../commit/def6e21))
- Send cancellation acknowledgment via MessageBus (Issue #52) ([b1fbe7e](../../commit/b1fbe7e))
- Address critical issues from Phase 4 code review ([30213fd](../../commit/30213fd))
- Resolve all remaining test failures (100% tests passing) ([1488138](../../commit/1488138))
- Resolve CI/CD workflow and test reliability issues ([9018e2d](../../commit/9018e2d))
- Resolve 21 AddressSanitizer stack-use-after-scope errors in coroutine tests ([e887541](../../commit/e887541))
- Resolve all native build and test failures (100% tests passing) ([c3ce7f3](../../commit/c3ce7f3))
- `packaging`: Resolve critical CPack packaging issues ([3f3e72e](../../commit/3f3e72e))
- Configure Docker UID/GID mapping for permission parity ([718f885](../../commit/718f885))
- `P1/P2`: Resolve profiling deadlock and fairness race condition ([72fc39d](../../commit/72fc39d))
- `P0`: Resolve critical safety issues — HealthCheckServer hang and coroutine UAF ([ffcc528](../../commit/ffcc528))

[Unreleased]: https://github.com/HomericIntelligence/Keystone/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/HomericIntelligence/Keystone/releases/tag/v0.1.0
