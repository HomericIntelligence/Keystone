# Contributing to ProjectKeystone

Thank you for contributing to ProjectKeystone! This guide will help you get set up and follow our development practices.

## Prerequisites

Before you start, ensure you have:

- **Docker** 20.10+ installed
- **docker-compose** 1.29+ (recommended)
- **Git** 2.30+
- **C++20 compiler** (automatically provided in Docker)
- Basic familiarity with C++20 and CMake

For detailed technology stack information, see [CLAUDE.md](CLAUDE.md#language-and-technology-stack).

## Development Workflow

We follow a **Test-Driven Development (TDD)** approach:

1. **Write the test first** (RED phase)
   - Create E2E tests in `tests/e2e/` or unit tests in appropriate directories
   - Tests should fail initially

2. **Implement the minimum code** to make tests pass (GREEN phase)
   - Add only the code required to pass the tests
   - No premature optimization or "nice-to-have" features

3. **Refactor** while keeping tests green (REFACTOR phase)
   - Improve code quality, readability, and structure
   - All tests must continue passing

4. **Commit** when tests pass
   - Use conventional commit format (see below)
   - Push to feature branch

For more details, see [CLAUDE.md - Development Workflow](CLAUDE.md#development-workflow).

## C++20 Code Standards

### Core Principles

- **Language**: Exclusively C++20. No Python, Mojo, or other languages
- **Naming**: `snake_case` for functions/variables, `PascalCase` for types
- **Memory**: Prefer `std::unique_ptr`, use `std::shared_ptr` only for shared ownership
- **Async**: Use C++20 coroutines for async operations (no raw threads)
- **RAII**: Always use Resource Acquisition Is Initialization

### Style Examples

```cpp
// Functions and variables: snake_case
void process_message(const KeystoneMessage& msg);
auto create_agent() -> std::unique_ptr<AgentBase>;

// Types: PascalCase
class ChiefArchitectAgent;
struct KeystoneMessage;

// Coroutines for async
Task<void> handle_message_async(const KeystoneMessage& msg) {
    co_await delegate_task(msg);
    co_return;
}

// Prefer unique_ptr for ownership
auto agent = std::make_unique<TaskAgent>("task1");

// References for non-owning access
void update_agent(const AgentBase& agent);
```

For comprehensive style guidelines, see [CLAUDE.md - Coding Standards](CLAUDE.md#coding-standards).

## Testing Requirements

**All contributions must meet ≥70% line coverage threshold (enforced in CI).**

### Running Tests Locally

```bash
# Build and run all tests
docker-compose up test

# Run tests with Docker dev environment
docker-compose up -d dev
docker-compose exec dev bash

# Inside container:
cd build
cmake -G Ninja ..
ninja
ctest --output-on-failure
```

### Test Types

1. **Unit Tests** - Test individual components
   - Location: `tests/unit/`
   - Framework: Google Test
   - Example: MessageQueue operations, ThreadPool functionality

2. **E2E Tests** - Test full agent hierarchies
   - Location: `tests/e2e/`
   - Framework: Google Test
   - Example: ChiefArchitect delegating to TaskAgent

3. **Sanitizer Tests** - Catch memory and threading issues
   - Run with: `docker-compose up test-asan` (AddressSanitizer)
   - Run with: `docker-compose up test-tsan` (ThreadSanitizer)

### Coverage Check

```bash
# Generate coverage report
docker-compose exec dev bash
cd build
cmake -G Ninja -DENABLE_COVERAGE=ON ..
ninja
ctest
./scripts/generate_coverage.sh
# Check report in: build/coverage-report/html/index.html
```

## Commit Message Format

Follow **Conventional Commits** format:

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Types

- `feat` - New feature
- `fix` - Bug fix
- `docs` - Documentation only
- `test` - Test additions or fixes
- `refactor` - Code refactoring without feature changes
- `perf` - Performance improvements
- `chore` - Build, dependencies, or tooling

### Examples

```
feat(agents): Add ComponentLeadAgent for Level 1 coordination

Implement ComponentLeadAgent to coordinate multiple ModuleLeadAgents.
Includes task decomposition and result aggregation across modules.

Closes #123
```

```
fix(message-bus): Resolve thread-safety issue in routeMessage()

Use mutex lock when accessing agent registry in concurrent scenarios.
Added test case for concurrent message routing.

Fixes #456
```

## Pull Request Process

1. **Create a feature branch**
   ```bash
   git checkout -b claude/feature-name-<session-id>
   ```

2. **Write tests first** (TDD approach)
   ```bash
   # Create failing tests
   git add tests/
   git commit -m "test: add tests for feature X"
   ```

3. **Implement code** to make tests pass
   ```bash
   # Implement feature
   git add src/
   git commit -m "feat(component): implement feature X"
   ```

4. **Ensure all quality gates pass** (see below)
   ```bash
   docker-compose up test
   # Verify: all tests pass, no memory leaks, sanitizers clean
   ```

5. **Push and create PR**
   ```bash
   git push origin claude/feature-name-<session-id>
   gh pr create --issue <issue-number>
   ```

6. **Verify PR is linked to issue**
   - Check GitHub issue page - should show PR in Development section
   - If missing, edit PR description to add "Closes #`issue-number`"

For detailed workflow, see [CLAUDE.md - Git Workflow](CLAUDE.md#git-workflow).

## Quality Gates

ProjectKeystone enforces **5 quality gates** that must pass before merge:

### 1. Code Coverage (≥70%)

**Requirement**: All code must have ≥70% line coverage

```bash
# Check locally
docker-compose exec dev bash -c "./scripts/generate_coverage.sh"
```

Tools: `lcov`, `gcov` (requires `-DENABLE_COVERAGE=ON`)

### 2. Static Analysis (Zero Critical Errors)

**Requirement**: No critical issues from static analysis

```bash
# Run locally
docker-compose exec dev bash -c "cd build && ./scripts/run_static_analysis.sh"
```

Tools: `clang-tidy`, `cppcheck`

### 3. Fuzz Testing (1 hour crash-free)

**Requirement**: No crashes in 1 hour of fuzzing

```bash
# Run locally
cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ ..
ninja
./fuzz_message_serialization -max_total_time=900
```

Tools: `libFuzzer` (requires Clang)

### 4. Performance Benchmarks (No >10% regressions)

**Requirement**: No performance regression >10% vs baseline

```bash
# Run locally (Release build)
cmake -DCMAKE_BUILD_TYPE=Release ..
./scripts/run_benchmarks.sh --compare benchmarks/results/baseline.json
```

Tools: Google Benchmark

### 5. Unit & E2E Tests (All passing)

**Requirement**: All 329+ tests must pass

```bash
docker-compose up test
```

For detailed quality gate descriptions, see [docs/CICD_QUALITY_GATES.md](docs/CICD_QUALITY_GATES.md).

## Development Environment

### Using Docker (Recommended)

```bash
# Start dev environment with mounted source
docker-compose up -d dev

# Enter container
docker-compose exec dev bash

# Inside container: build and test
cd build
cmake -G Ninja ..
ninja
ctest --output-on-failure

# Exit when done
exit
docker-compose down
```

### Project Structure

```
ProjectKeystone/
├── CMakeLists.txt           # Build configuration
├── CLAUDE.md                # Project overview & guidelines
├── CONTRIBUTING.md          # This file
├── include/                 # Public headers
│   ├── agents/              # Agent class definitions
│   ├── core/                # Message bus, messaging
│   └── concurrency/         # Thread pool, coroutines
├── src/                     # Implementation
├── tests/                   # Test files
│   ├── unit/                # Unit tests
│   └── e2e/                 # E2E tests
└── docs/                    # Documentation & planning
    ├── plan/                # Architecture decisions (ADRs)
    └── CICD_QUALITY_GATES.md
```

## Getting Help

- **Project Overview**: See [CLAUDE.md](CLAUDE.md)
- **Development Details**: See [docs/plan/](docs/plan/)
- **Architecture Decisions**: See [docs/plan/adr/](docs/plan/adr/)
- **Quality Standards**: See [docs/CICD_QUALITY_GATES.md](docs/CICD_QUALITY_GATES.md)

## Key Resources

1. **CLAUDE.md** - Complete project overview, architecture, and development workflow
2. **Testing Strategy** - [docs/plan/testing-strategy.md](docs/plan/testing-strategy.md)
3. **Architecture Decisions**:
   - [ADR-001: MessageBus Architecture](docs/plan/adr/ADR-001-message-bus-architecture.md)
   - [ADR-002: Shared Pointer Migration](docs/plan/adr/ADR-002-shared-ptr-migration.md)
   - [ADR-003: Async Agent Unification](docs/plan/adr/ADR-003-async-agent-unification.md)
4. **TDD Roadmap** - [docs/plan/TDD_FOUR_LAYER_ROADMAP.md](docs/plan/TDD_FOUR_LAYER_ROADMAP.md)

---

**Questions?** Check [CLAUDE.md](CLAUDE.md) for comprehensive project guidelines and development practices.
