set shell := ["bash", "-c"]

default:
  @just --list

# Bootstrap the local notes/ scratch workspace (not version-controlled).
# Required for .claude/agents/* workflows that write to /notes/issues/<N>.
setup-notes:
  ./scripts/setup-notes.sh

# The C++ build toolchain (cmake/ninja/conan/gcovr) is provided by uv-locked
# PyPI wheels (Odysseus ADR-018); `uv run` puts them on PATH for the whole
# recipe, including tools invoked transitively by `make`. Run `uv sync` once
# first. The compiler (gcc/g++), clang tools, and lcov come from the system.
build:
  uv run make compile

test:
  uv run make test

# Install Conan dependencies (tsan profile).
# The tsan Conan profile sets user.keystone.sanitizer=tsan and
# tools.cmake.cmake_layout:build_folder_vars so Conan names its generated
# preset "conan-debug-tsan" rather than "conan-debug", preventing a
# duplicate-preset collision with the plain debug build in CMakeUserPresets.json
# regardless of whether conan install is invoked via just or directly.
deps-tsan:
    uv run conan install . \
        --output-folder=build/tsan \
        --profile=conan/profiles/tsan \
        --build=missing

# Build with ThreadSanitizer
build-tsan: deps-tsan
    uv run cmake --preset tsan
    uv run cmake --build --preset tsan

# Run tests under ThreadSanitizer
test-tsan: build-tsan
    uv run ctest --preset tsan --output-on-failure


lint:
  uv run make lint

# Enforces ADR-015 (agent layer) + ADR-016 (Python orchestration) extraction.
# Fails if any extracted artifact reappears in the tree.
check-extraction:
  ./scripts/check-extraction.sh

# Validate that required workflows and activation docs remain merge-queue ready.
check-merge-queue-readiness:
  ./scripts/check-merge-queue-readiness.sh

# Fail-closed GitHub Actions schema validation for both workflow extensions.
check-workflow-schema:
  ./scripts/check-workflow-schema.sh

# Prove that semantic workflow-schema failures are rejected.
test-workflow-schema-validation:
  ./scripts/test-workflow-schema-validation.sh

format:
  uv run make format

format-check:
  uv run make format.check

# Run NATS integration tests (requires NATS server at NATS_URL)
# Starts a NATS server via Docker Compose, runs the nats_integration_tests
# target, then tears down the server.
integration-test:
    #!/usr/bin/env bash
    set -euo pipefail
    uv run cmake --preset debug
    uv run cmake --build --preset debug --target nats_integration_tests
    echo "--- Starting NATS test server ---"
    podman compose -f docker-compose.test.yml up -d nats
    trap 'echo "--- Stopping NATS test server ---"; podman compose -f docker-compose.test.yml down' EXIT
    # Wait for NATS to be ready (up to 30 s)
    for i in $(seq 1 30); do
        if curl -sf http://localhost:8222/healthz > /dev/null 2>&1; then
            echo "NATS server is healthy."
            break
        fi
        echo "Waiting for NATS server... ($i/30)"
        sleep 1
    done
    KEYSTONE_INTEGRATION_TESTS=1 NATS_URL="${NATS_URL:-nats://localhost:4222}" \
        uv run ctest --preset debug --tests-regex nats_integration --output-on-failure

# Build with coverage instrumentation
coverage:
    uv run cmake --preset coverage
    uv run cmake --build --preset coverage

# Full CI build (release + warnings-as-errors)
ci:
    uv run cmake --preset ci
    uv run cmake --build --preset ci
    uv run ctest --preset ci

# Type-check Python files (conanfile.py) with mypy
typecheck:
    uv run mypy conanfile.py


# Check that the tests/ directory structure matches what is documented in AGENTS.md
check-docs-tree:
    #!/usr/bin/env bash
    set -euo pipefail
    ACTUAL=$(find tests/ -maxdepth 1 -mindepth 1 -type d | sed 's|tests/||' | sort)
    DOCUMENTED=$(grep -A20 'Test Structure' AGENTS.md | grep '├──\|└──' | sed 's/.*── //;s|/.*||' | sort)
    DIFF=$(diff <(echo "$DOCUMENTED") <(echo "$ACTUAL") || true)
    if [ -n "$DIFF" ]; then
        echo "AGENTS.md 'Test Structure' block is out of sync with actual tests/ subdirectories."
        echo "Diff (documented vs actual):"
        echo "$DIFF"
        exit 1
    fi
    echo "AGENTS.md Test Structure block matches actual tests/ layout."

# Regenerate conan.lock from conanfile.py
update-conan-lock:
    uv run conan lock create conanfile.py --lockfile-out conan.lock

clean:
  make clean

start:
  make container.up

status:
  podman compose ps

benchmark:
  uv run make benchmark

# Validate CPack package generation without producing real packages (issue #370).
# Builds the release preset, then runs cpack --debug -G TGZ so packaging errors
# are caught early, before a real release fires.  Runs from build/release.
pack-dry-run:
    #!/usr/bin/env bash
    set -euo pipefail
    uv run cmake --preset release
    uv run cmake --build --preset release
    echo "--- CPack dry-run (TGZ, no output) ---"
    cd build/release && cpack --debug -G TGZ 2>&1 | grep -v "^CPack: -"
    echo "--- CPack dry-run complete ---"

# === Containerized CI (podman by default) ===

# Build the CI container image (podman first, docker fallback)
ci-build:
    podman build --ignorefile ci/.dockerignore -f ci/Containerfile -t keystone-ci:local . || docker build -f ci/Containerfile -t keystone-ci:local .

# Run CI lint checks in container
ci-lint:
    ./scripts/run_ci_local.sh lint

# Run CI markdownlint checks in container
ci-markdownlint:
    ./scripts/run_ci_local.sh markdownlint

# Run CI unit-tests checks in container
ci-unit-tests:
    ./scripts/run_ci_local.sh unit-tests

# Run CI integration-tests checks in container
ci-integration-tests:
    ./scripts/run_ci_local.sh integration-tests

# Run CI schema-validation checks in container
ci-schema-validation:
    ./scripts/run_ci_local.sh schema-validation

# Run CI security-secrets-scan checks in container
ci-security-secrets-scan:
    ./scripts/run_ci_local.sh security-secrets-scan

# Run CI deps-version-sync checks in container
ci-deps-version-sync:
    ./scripts/run_ci_local.sh deps-version-sync

# Run CI forbid-suppressions checks in container
ci-forbid-suppressions:
    ./scripts/run_ci_local.sh forbid-suppressions

# Run CI justfile-check checks in container
ci-justfile-check:
    ./scripts/run_ci_local.sh justfile-check

# Run CI symlink-check checks in container
ci-symlink-check:
    ./scripts/run_ci_local.sh symlink-check

# Run all CI checks in container
ci-all:
    ./scripts/run_ci_local.sh all
