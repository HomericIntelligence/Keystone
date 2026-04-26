set shell := ["bash", "-c"]

default:
  @just --list

build:
  make compile NATIVE=1

test:
  make test NATIVE=1

# Install Conan dependencies (tsan profile).
# The tsan Conan profile sets user.keystone.sanitizer=tsan and
# tools.cmake.cmake_layout:build_folder_vars so Conan names its generated
# preset "conan-debug-tsan" rather than "conan-debug", preventing a
# duplicate-preset collision with the plain debug build in CMakeUserPresets.json
# regardless of whether conan install is invoked via just or directly.
deps-tsan:
    conan install . \
        --output-folder=build/tsan \
        --profile=conan/profiles/tsan \
        --build=missing

# Build with ThreadSanitizer
build-tsan: deps-tsan
    cmake --preset tsan
    cmake --build --preset tsan

# Run tests under ThreadSanitizer
test-tsan: build-tsan
    ctest --preset tsan --output-on-failure


lint:
  make lint NATIVE=1

format:
  make format NATIVE=1

format-check:
  make format.check NATIVE=1

# Run NATS integration tests (requires NATS server at NATS_URL)
# Starts a NATS server via Docker Compose, runs the nats_integration_tests
# target, then tears down the server.
integration-test:
    #!/usr/bin/env bash
    set -euo pipefail
    cmake --preset debug
    cmake --build --preset debug --target nats_integration_tests
    echo "--- Starting NATS test server ---"
    docker-compose -f docker-compose.test.yml up -d nats
    trap 'echo "--- Stopping NATS test server ---"; docker-compose -f docker-compose.test.yml down' EXIT
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
        ctest --preset debug --tests-regex nats_integration --output-on-failure

# Build with coverage instrumentation
coverage:
    cmake --preset coverage
    cmake --build --preset coverage

# Full CI build (release + warnings-as-errors)
ci:
    cmake --preset ci
    cmake --build --preset ci
    ctest --preset ci

# Type-check Python files (conanfile.py) with mypy
typecheck:
    mypy conanfile.py


# Check that the tests/ directory structure matches what is documented in CLAUDE.md
check-docs-tree:
    #!/usr/bin/env bash
    set -euo pipefail
    ACTUAL=$(find tests/ -maxdepth 1 -mindepth 1 -type d | sed 's|tests/||' | sort)
    DOCUMENTED=$(grep -A20 'Test Structure' CLAUDE.md | grep '├──\|└──' | sed 's/.*── //;s|/.*||' | sort)
    DIFF=$(diff <(echo "$DOCUMENTED") <(echo "$ACTUAL") || true)
    if [ -n "$DIFF" ]; then
        echo "CLAUDE.md 'Test Structure' block is out of sync with actual tests/ subdirectories."
        echo "Diff (documented vs actual):"
        echo "$DIFF"
        exit 1
    fi
    echo "CLAUDE.md Test Structure block matches actual tests/ layout."

# Regenerate conan.lock from conanfile.py
update-conan-lock:
    conan lock create conanfile.py --lockfile-out conan.lock

clean:
  make clean

start:
  make docker.up

status:
  docker-compose ps

benchmark:
  make benchmark NATIVE=1

# Validate CPack package generation without producing real packages (issue #370).
# Builds the release preset, then runs cpack --debug -G TGZ so packaging errors
# are caught early, before a real release fires.  Runs from build/release.
pack-dry-run:
    #!/usr/bin/env bash
    set -euo pipefail
    cmake --preset release
    cmake --build --preset release
    echo "--- CPack dry-run (TGZ, no output) ---"
    cd build/release && cpack --debug -G TGZ 2>&1 | grep -v "^CPack: -"
    echo "--- CPack dry-run complete ---"
