set shell := ["bash", "-c"]

default:
  @just --list

build:
  make compile NATIVE=1

test:
  make test NATIVE=1

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

clean:
  make clean

start:
  make docker.up

status:
  docker-compose ps

benchmark:
  make benchmark NATIVE=1

ci:
  make ci NATIVE=1
