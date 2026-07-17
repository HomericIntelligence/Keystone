# Keystone Documentation

Welcome to the Keystone Hierarchical Multi-Agent System (HMAS) documentation.

## Quick Navigation

- [Getting Started](#getting-started)
- [Architecture & Design](#architecture--design)
- [Development](#development)
- [Testing & Quality](#testing--quality)
- [Deployment & Operations](#deployment--operations)
- [Phase History](#phase-history)
- [Additional Resources](#additional-resources)

---

## Getting Started

### New to Keystone?

Start here to understand what Keystone is and how to build it.

- **[Main README](../README.md)** - Project overview, quick start, and build instructions
- **[CLAUDE.md](../CLAUDE.md)** - Comprehensive project guide for Claude Code AI
- **[Installation Verification](verify-installation.md)** - Step-by-step installation guide

### Quick Start

```bash
# Build and test
docker-compose up test

# Development environment
docker-compose up -d dev
docker-compose exec dev bash
```

See [Main README](../README.md#quick-start) for detailed instructions.

---

## Architecture & Design

### Core Architecture

Understand the foundational design principles and architecture decisions.

#### Primary Documentation

- **[Four-Layer Architecture](plan/FOUR_LAYER_ARCHITECTURE.md)** - Complete system architecture specification
  - 4-layer agent hierarchy (L0: ChiefArchitect → L1: ComponentLead → L2: ModuleLead → L3: TaskAgent)
  - Agent responsibilities and communication patterns
  - Message flow and coordination

- **[TDD Four-Layer Roadmap](plan/TDD_FOUR_LAYER_ROADMAP.md)** - Test-driven development approach
  - Phase-by-phase implementation strategy
  - E2E test specifications for each phase
  - Success criteria and deliverables

- **[Implementation Plan](plan/README.md)** - Executive summary and project overview
  - Project vision and goals
  - Technology stack
  - Team requirements and timeline

#### Specialized Architecture

- **[Async Work-Stealing Architecture](plan/ASYNC_WORK_STEALING_ARCHITECTURE.md)** - C++20 coroutine-based scheduler
  - Lock-free work-stealing queues
  - Per-thread task scheduling
  - LIFO cache locality optimizations

- **[Simulation Architecture](plan/SIMULATION_ARCHITECTURE.md)** - Distributed work-stealing simulation
  - NUMA-aware task distribution
  - Remote vs local stealing strategies
  - Performance modeling

- **[Work-Stealing Migration Plan](plan/WORK_STEALING_MIGRATION_PLAN.md)** - Migration from sync to async agents

### Architecture Decision Records (ADRs)

Key architectural decisions with context, rationale, and consequences.

- **[ADR-001: MessageBus Architecture](plan/adr/ADR-001-message-bus-architecture.md)**
  - Central routing hub for agent communication
  - Decoupled agent interaction
  - Synchronous vs async routing

- **[ADR-002: Work-Stealing Scheduler Architecture](plan/adr/ADR-002-work-stealing-scheduler-architecture.md)**
  - Per-worker queues for efficient load balancing
  - Cache-optimized access patterns
  - Coroutine support for scalable agent execution

- **[ADR-003: Async Agent Unification](plan/adr/ADR-003-async-agent-unification.md)**
  - Unified coroutine-based agent interface
  - Backward compatibility strategy
  - Performance comparison (sync vs async)

- **[ADR-010: Architecture Issue Resolution](plan/adr/ADR-010-architecture-issue-resolution.md)**
  - Critical architecture issues (P0) resolved
  - C1-C5 fixes implementation
  - System stability improvements

- **[ADR-011: Phase 6 Architecture Review Fixes](plan/adr/ADR-011-phase-6-architecture-review-fixes.md)**
  - Kubernetes architecture review findings
  - Phase 6.6 fixes and improvements
  - Production readiness enhancements

---

## Development

### Build System & Tools

- **[Build System](plan/build-system.md)** - CMake configuration and toolchain setup
  - CMake 3.20+ configuration
  - Compiler requirements (C++20)
  - Docker build system
  - Dependency management

- **[Module Structure](plan/modules.md)** - C++20 module organization
  - Module dependencies
  - Public/private interface design
  - Header organization

### Development Guides

- **[Docker Testing](DOCKER_TESTING.md)** - Docker testing guide and instructions
  - Container build verification
  - Multi-stage build testing
  - Development environment setup

- **[C2/C3 Architecture Migration](plan/MIGRATION_ARCHITECTURE_C2_C3.md)** - Migration guide for architecture fixes
  - C2/C3 architecture migration
  - Critical issue resolution
  - System refactoring approach

- **[Error Handling](plan/error-handling.md)** - Error handling patterns
  - Exception strategy
  - Result types and `std::optional`
  - Error propagation in async code

- **[Risk Analysis](plan/risks.md)** - Project risks and mitigation strategies
  - Technical risks
  - Schedule risks
  - Mitigation plans

### Scripts

Automation scripts for common development tasks (see `../scripts/`):

- `generate_coverage.sh` - Generate code coverage reports
- `run_benchmarks.sh` - Run performance benchmarks
- `run_static_analysis.sh` - Run clang-tidy and cppcheck
- `test_docker.sh` - Automated Docker testing
- `verify_install.sh` - Verify installation steps

---

## Testing & Quality

### Testing Strategy

- **[Testing Strategy](plan/testing-strategy.md)** - Comprehensive testing approach
  - Unit tests (Google Test)
  - E2E integration tests
  - Performance benchmarks
  - Fuzz testing
  - Chaos engineering

### Test Documentation

- **[Benchmarks README](../benchmarks/README.md)** - Performance benchmarking guide
  - 45+ benchmark tests across 5 suites
  - Running benchmarks in Release mode
  - Regression detection
  - Baseline establishment

- **[Fuzz Testing README](../fuzz/README.md)** - Fuzz testing guide
  - 4 libFuzzer targets
  - Building with fuzzing enabled
  - Corpus management
  - CI/CD integration

### Quality Gates

- **[CI/CD Quality Gates](CICD_QUALITY_GATES.md)** - Automated quality enforcement
  - Code coverage (≥95%)
  - Static analysis (zero critical errors)
  - Fuzz testing (1 hour, no crashes)
  - Performance benchmarks (no >10% regressions)
  - All tests passing (329+ tests)

### Coverage & Metrics

- **[Phase 9 Coverage Baseline](plan/PHASE_9_COVERAGE_BASELINE.md)** - Current coverage metrics
  - Line coverage: 86.2%
  - Function coverage: 88.5%
  - Tests passing: 325/329 (98.8%)

---

## Deployment & Operations

### Deployment Guides

- **[Kubernetes Deployment](KUBERNETES_DEPLOYMENT.md)** - Deploy to Kubernetes
  - Helm charts
  - Multi-node setup
  - Service discovery
  - Scaling strategies

- **[Production Deployment](PRODUCTION.md)** - Production deployment guide
  - Production readiness checklist
  - Security considerations
  - Resource requirements
  - High availability setup

### Monitoring & Operations

- **[Monitoring Guide](MONITORING.md)** - Monitoring and observability
  - Prometheus metrics
  - Grafana dashboards
  - Alerting rules
  - Performance tracking

---

## Phase History

### Completed Phases

Keystone follows a phased TDD approach. Each phase builds incrementally on the previous.

#### Core System (Phases 1-5)

- **[Phase 5 Complete](plan/PHASE_5_COMPLETE.md)** - Chaos engineering and resilience
  - Retry policies
  - Circuit breakers
  - Heartbeat monitoring
  - Chaos testing

#### Performance Optimization (Phase D)

- **[Phase A-D Completion](phases/phase-A-D-completion.md)** - Production-ready HMAS
  - Phase A: Async work-stealing architecture
  - Phase B: Async agent migration
  - Phase C: Distributed simulation
  - Phase D: Performance profiling and optimization
  - 329 tests passing (100%)
  - 20,335 lines added

#### Distributed Features (Phase 8 - Optional)

- **[Phase 8 Complete](PHASE_8_COMPLETE.md)** - Distributed multi-node communication
- **[Phase 8 Completion Summary](phases/phase-8-completion-summary.md)** - Comprehensive completion record
- **[Phase 8 Optional Build](PHASE_8_OPTIONAL_BUILD.md)** - Build instructions for distributed features
- **[Phase 8 Implementation Summary](PHASE_8_IMPLEMENTATION_SUMMARY.md)** - Implementation details
- **[Phase 8 E2E Tests](PHASE_8_E2E_TESTS.md)** - 26 distributed E2E tests
- **[Phase 8 Final Status](PHASE_8_FINAL_STATUS.md)** - Final validation and metrics
- **[Phase 8 Progress](PHASE_8_PROGRESS.md)** - Development progress tracker

**Phase 8 Features** (optional, disabled by default):

- gRPC-based distributed communication
- Protocol Buffers message serialization
- YAML task specification format
- ServiceRegistry for multi-node discovery
- Load balancing strategies
- Result aggregation
- Docker Compose multi-node deployment

**Phase 8 Documentation**:

- **[Network Protocol](NETWORK_PROTOCOL.md)** - gRPC API specification
- **[YAML Specification](YAML_SPECIFICATION.md)** - YAML task format

### Future Phases

- **[Phase 4 Plan](plan/PHASE_4_PLAN.md)** - Multi-component system expansion
- **[Phase 5 Plan](plan/PHASE_5_PLAN.md)** - Chaos engineering (completed)
- **[Phase 6 Plan](plan/PHASE_6_PLAN.md)** - Advanced resilience features
- **[Phase 7 Plan](plan/PHASE_7_PLAN.md)** - Production hardening
- **[Phase 9 Plan](plan/PHASE_9_PLAN.md)** - Enhanced testing & quality (in progress)
- **[Phase 9 2-5 Complete](plan/PHASE_9_2-5_COMPLETE.md)** - Milestones 2-5 completion
- **[Phase 10 Plan](plan/PHASE_10_PLAN.md)** - Future enhancements

---

## Additional Resources

### Project Status

**Current Phase**: Phase 9 (Enhanced Testing & Quality)

**Metrics**:

- Line Coverage: 86.2% (target: ≥95%)
- Function Coverage: 88.5% (target: ≥90%)
- Tests: 329 tests (325/329 passing - 98.8%)
- Benchmarks: 5 suites, 45+ tests
- Fuzz Targets: 4 targets

**Last Updated**: 2025-11-19

### External Links

- **[GitHub Repository](https://github.com/mvillmow/Keystone)**
- **[CI/CD Workflows](../.github/workflows/)** - GitHub Actions workflows

### Technology Stack

- **Language**: C++20 (GCC 13+ or Clang 15+)
- **Build System**: CMake 3.20+, Ninja
- **Testing**: Google Test, Google Benchmark, libFuzzer
- **Libraries**: spdlog, Cista, concurrentqueue
- **Containerization**: Docker, Docker Compose

### Contributing

See [CLAUDE.md](../CLAUDE.md#development-workflow) for:

- Development workflow
- TDD approach
- Git workflow and branching
- Pull request creation
- Coding standards

---

## Documentation Organization

This documentation follows a hierarchical structure:

```
docs/
├── README.md                    # This file - documentation hub
├── plan/                        # Architecture and planning
│   ├── README.md                # Implementation plan overview
│   ├── FOUR_LAYER_ARCHITECTURE.md
│   ├── TDD_FOUR_LAYER_ROADMAP.md
│   ├── adr/                     # Architecture Decision Records
│   ├── PHASE_*.md               # Phase plans and completions
│   └── *.md                     # Supporting docs
├── phases/                      # Phase completion archives
├── CICD_QUALITY_GATES.md        # Quality gate specifications
├── KUBERNETES_DEPLOYMENT.md     # Kubernetes deployment guide
├── PRODUCTION.md                # Production deployment guide
├── MONITORING.md                # Monitoring and observability
├── PHASE_8_*.md                 # Phase 8 distributed features
├── NETWORK_PROTOCOL.md          # gRPC protocol specification
└── YAML_SPECIFICATION.md        # YAML task format
```

### Documentation Principles

1. **Start with README.md** - This documentation hub provides navigation
2. **Follow the hierarchy** - Use the sections above to find what you need
3. **Check phase history** - Understand what's been completed
4. **Read ADRs for decisions** - Understand why architectural choices were made
5. **Use scripts for automation** - Leverage provided scripts for common tasks

---

## Getting Help

### Documentation Issues

If you find missing or incorrect documentation:

1. Check if it's covered in a different section
2. Look in phase completion docs for historical context
3. Check ADRs for architectural decisions
4. File an issue in the GitHub repository

### Development Questions

- Review [CLAUDE.md](../CLAUDE.md) for comprehensive project context
- Check [Testing Strategy](plan/testing-strategy.md) for testing questions
- See [Build System](plan/build-system.md) for build issues
- Review [CI/CD Quality Gates](CICD_QUALITY_GATES.md) for quality standards

---

**Welcome to Keystone!** 🚀
