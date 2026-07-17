# Docker Testing Guide

This document provides instructions for testing the Docker setup for Keystone HMAS.

## Prerequisites

Ensure you have Docker installed:

```bash
docker --version    # Should be 20.10+
docker-compose --version  # Should be 1.29+
```

## Test Plan

### Test 1: Build Runtime Image

```bash
# Build the runtime image (includes build stage)
docker build --target runtime -t keystone:latest .

# Expected output:
# - Successfully builds builder stage
# - Successfully builds runtime stage
# - Final image size should be < 200MB
```

### Test 2: Run Tests in Container

```bash
# Run the tests
docker run --rm keystone:latest

# Expected output:
# [==========] Running 3 tests from 1 test suite.
# ...
# [  PASSED  ] 3 tests.
```

### Test 3: Docker Compose - Test Service

```bash
# Build and run tests using docker-compose
docker-compose up test

# Expected output:
# - Builds image if not exists
# - Runs phase1_e2e_tests
# - All 3 tests pass
# - Container exits with code 0
```

### Test 4: Docker Compose - Development Environment

```bash
# Start development container
docker-compose up -d dev

# Enter the container
docker-compose exec dev bash

# Inside container, build and test:
cd build
cmake -G Ninja ..
ninja
./phase1_e2e_tests

# Exit
exit

# Stop container
docker-compose down
```

### Test 5: Build from Scratch (No Cache)

```bash
# Clean build (no cache)
docker-compose build --no-cache test

# Run tests
docker-compose up test
```

## Expected Results

All tests should pass with output similar to:

```
[==========] Running 3 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 3 tests from E2E_Phase1
[ RUN      ] E2E_Phase1.ChiefArchitectDelegatesToTaskAgent

Test: Adding X + Y = Z
✓ TaskAgent correctly computed: X + Y = Z
[       OK ] E2E_Phase1.ChiefArchitectDelegatesToTaskAgent (X ms)
[ RUN      ] E2E_Phase1.ChiefArchitectSendsMultipleCommands
✓ 5 + 3 = 8
✓ 15 + 27 = 42
✓ 100 + 200 = 300
[       OK ] E2E_Phase1.ChiefArchitectSendsMultipleCommands (X ms)
[ RUN      ] E2E_Phase1.TaskAgentHandlesCommandErrors
✓ TaskAgent correctly handled invalid command with error: ...
[       OK ] E2E_Phase1.TaskAgentHandlesCommandErrors (X ms)
[----------] 3 tests from E2E_Phase1 (X ms total)

[----------] Global test environment tear-down
[==========] 3 tests from 1 test suite ran. (X ms total)
[  PASSED  ] 3 tests.
```

## Troubleshooting

### Issue: "docker: command not found"

**Solution**: Install Docker from <https://docs.docker.com/get-docker/>

### Issue: "permission denied while trying to connect to the Docker daemon socket"

**Solution**: Add user to docker group: `sudo usermod -aG docker $USER`

### Issue: Build fails with CMake errors

**Solution**: Check CMakeLists.txt syntax and ensure all source files exist

### Issue: Tests fail to run

**Solution**:

- Check that executables are copied correctly in Dockerfile COPY command
- Verify source files compile without errors
- Check that Google Test is properly fetched and linked

## Cleanup

```bash
# Stop all containers
docker-compose down

# Remove volumes
docker-compose down -v

# Remove images
docker rmi keystone:latest keystone-dev:latest

# Remove dangling images
docker image prune -f
```

## CI/CD Integration

For CI/CD pipelines (GitHub Actions, GitLab CI, etc.):

```yaml
# Example GitHub Actions
- name: Build and test
  run: |
    docker-compose build test
    docker-compose up --exit-code-from test test
```

---

**Note**: These tests assume Docker is available in the environment. The current development environment may not have
Docker installed.
