# Contributing to Keystone

Thank you for contributing to Keystone — the invisible transport layer of the
HomericIntelligence ecosystem. This guide covers everything you need to get started.

## Prerequisites

Before you begin, ensure you have:

- **C++20 compiler** (Clang 17+ or GCC 13+)
- **[uv](https://docs.astral.sh/uv/)** — manages the pinned build toolchain
  (CMake, Ninja, Conan, gcovr, pre-commit) as locked PyPI wheels (Odysseus ADR-018)
- **just** (task runner — `cargo install just` or see [just releases](https://github.com/casey/just/releases))
- **Git** 2.30+
- **clang-format** and **clang-tidy** (matching the project's `.clang-format` / `.clang-tidy` config)

For the full technology stack, see [AGENTS.md](AGENTS.md#language-and-technology-stack).

### Installing Prerequisites

`uv sync` installs the locked CMake/Ninja/Conan/gcovr/pre-commit toolchain; the
C++ compiler, clang-format/clang-tidy, lcov, and the OpenSSL dev headers come
from your system package manager. gtest is provided by Conan (see `conanfile.py`).

**Ubuntu/Debian:**

```bash
sudo apt-get install -y clang-18 clang++-18 libc++-18-dev clang-format-18 clang-tidy-18 lcov libssl-dev
uv sync            # installs the locked CMake/Ninja/Conan/gcovr toolchain
```

**macOS (Homebrew):**

```bash
brew install llvm lcov
uv sync            # installs the locked CMake/Ninja/Conan/gcovr toolchain
```

**`just` (all platforms):**

```bash
cargo install just
# or: brew install just / apt-get install just
```

## Branch Strategy

**Never commit directly to `main`.**

```bash
# New feature
git checkout -b feat/short-description

# Bug fix
git checkout -b fix/issue-description

# Documentation
git checkout -b docs/description

# Chore / maintenance
git checkout -b chore/description
```

Branch names should be lowercase, hyphenated, and describe the change.

## Development Workflow

Keystone follows **Test-Driven Development (TDD)**:

1. **RED** — write a failing test for the behaviour you're adding
2. **GREEN** — write the minimum code to make it pass
3. **REFACTOR** — clean up while keeping tests green
4. **Commit** using the conventional format below

## Building the Project

1. **Refactor** while keeping tests green (REFACTOR phase)
   - Improve code quality, readability, and structure
   - All tests must continue passing

2. **Commit** when tests pass
   - Use conventional commit format (see below)
   - Push to feature branch

For more details, see [AGENTS.md - Development Workflow](AGENTS.md#development-workflow).

## C++20 Code Standards

### Core Principles

- **Language**: Exclusively C++20. No Python, Mojo, or other languages
- **Naming**: `camelBack` for functions, `lower_case` for variables/members/parameters, `PascalCase` for types
- **Memory**: Prefer `std::unique_ptr`, use `std::shared_ptr` only for shared ownership
- **Async**: Use C++20 coroutines for async operations (no raw threads)
- **RAII**: Always use Resource Acquisition Is Initialization

### Style Examples

```cpp
// Functions: camelBack
void processMessage(const KeystoneMessage& msg);
auto createAgent() -> std::unique_ptr<AgentBase>;

// Types: PascalCase
class ChiefArchitectAgent;
struct KeystoneMessage;

// Coroutines for async
Task<void> handleMessageAsync(const KeystoneMessage& msg) {
    co_await delegateTask(msg);
    co_return;
}

// Prefer unique_ptr for ownership
auto agent = std::make_unique<TaskAgent>("task1");

// References for non-owning access
void updateAgent(const AgentBase& agent);
```

For comprehensive style guidelines, see [AGENTS.md - Coding Standards](AGENTS.md#coding-standards).

## Testing Requirements

**All contributions must meet ≥80% line coverage threshold (enforced in CI).**

### Running Tests Locally

Use CMakePresets for all builds:

```bash
# Install the locked build toolchain (first time)
uv sync

# Install C++ dependencies (first time / after conanfile.py changes)
uv run conan install . --build=missing -pr default

# Configure and build (uv run puts the toolchain on PATH)
uv run cmake --preset debug
uv run cmake --build --preset debug

# Or use the justfile shortcuts (they wrap the toolchain in `uv run`)
just build          # debug build
just build-release  # release build
```

## Running Tests

```bash
just test           # Run all tests (debug build)
just test-asan      # Run under AddressSanitizer
just test-tsan      # Run under ThreadSanitizer
```

Tests must pass under **both ASan and TSan** before a PR can be merged.

To run a specific preset manually:

```bash
uv run cmake --preset asan && uv run cmake --build --preset asan && uv run ctest --preset asan --output-on-failure
uv run cmake --preset tsan && uv run cmake --build --preset tsan && uv run ctest --preset tsan --output-on-failure
```

## Code Standards

### Language

This project is **exclusively C++20**. Do not add Python, Mojo, gRPC, or any other
language to the implementation.

### Naming Conventions

| Element | Style |
|---------|-------|
| Functions | `camelBack` |
| Variables / members / parameters | `lower_case` |
| Types / classes | `PascalCase` |
| Constants / enums | `kPascalCase` |
| Private members | `lower_case_` (trailing underscore) |

### Memory Management

- Prefer `std::unique_ptr` for single ownership.
- Use `std::shared_ptr` only when ownership is genuinely shared.
- No raw `new` / `delete` — always use RAII wrappers.

### Async

- Use C++20 coroutines for async operations.
- Prefer `co_await` over raw thread synchronisation primitives.

### Comments

Default to **no comments**. Add one only when the *why* is non-obvious: a hidden
constraint, a subtle invariant, or a workaround for a specific upstream bug. If
removing a comment wouldn't confuse a future reader, don't write it.

## Formatting and Linting

All source must pass `clang-format` and `clang-tidy` with zero warnings/errors.
Both are treated as errors in CI.

```bash
just format        # Format all source files in-place
just format-check  # Check formatting without modifying (CI gate)
just lint          # Run clang-tidy
```

Fix all formatting and lint issues before pushing. CI will reject PRs with violations.

## Commit Message Format (Conventional Commits)

```
<type>(<scope>): <subject>

<optional body>

<optional footer>
```

| Type | When to use |
|------|-------------|
| `feat` | New capability |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `test` | Test additions or changes |
| `refactor` | Internal restructure, no behaviour change |
| `perf` | Performance improvements |
| `chore` | Build, deps, CI, tooling |

Subject line: present tense, ≤72 characters, no trailing period.

**Examples:**

```
feat(bridge): add transparent local-to-NATS promotion

fix(message-bus): resolve data race on shutdown path

Fixes #42
```

## Pull Request Checklist

Before opening a PR:

- [ ] On a feature branch (`git branch --show-current`)
- [ ] All tests pass under ASan: `just test-asan`
- [ ] All tests pass under TSan: `just test-tsan`
- [ ] Formatting clean: `just format-check`
- [ ] clang-tidy clean: `just lint`
- [ ] No compilation warnings (`-Werror` is enforced)
- [ ] PR description references the issue (`Closes #N`)

## Project Structure

```
Keystone/
├── CMakeLists.txt        # Root build configuration
├── CMakePresets.json     # Preset-based build config (v8 schema)
├── conanfile.py          # Conan 2 dependency manifest
├── justfile              # Common task shortcuts
├── include/              # Public headers
│   └── keystone/         # Transport primitives
├── src/                  # Implementation
├── tests/                # GoogleTest unit and integration tests
│   ├── unit/
│   └── integration/
├── benchmarks/           # Google Benchmark suites
├── docs/                 # Architecture docs and ADRs
│   └── plan/adr/
└── AGENTS.md             # Full project guidelines
```

## Getting Help

- **Project overview and architecture**: [AGENTS.md](AGENTS.md)
- **Architecture Decision Records**: [docs/plan/adr/](docs/plan/adr/)
- **Quality gate details**: [docs/CICD_QUALITY_GATES.md](docs/CICD_QUALITY_GATES.md)
- **CI workflow documentation**: [.github/workflows/README.md](.github/workflows/README.md)

---

**Questions or concerns?** Open a GitHub Discussion or check [AGENTS.md](AGENTS.md) for
comprehensive project guidelines.
