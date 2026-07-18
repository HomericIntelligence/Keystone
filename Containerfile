# Keystone HMAS - C++20 Build Environment
# Multi-stage build for efficient image size

# ── uv binary source ──────────────────────────────────────────────────────────
# Pulled as a named stage so `COPY --from=uv` resolves identically under both
# podman/buildah and docker. A bare `COPY --from=ghcr.io/astral-sh/uv:<tag>@<digest>`
# (tag AND digest together) is rejected by buildah with "no stage or image found
# with that name", so we alias the digest-pinned image to a stage name here and
# COPY from the alias. Keep this pin in sync with astral-sh/setup-uv in
# .github/workflows/*.yml when bumping (Odysseus ADR-018).
FROM ghcr.io/astral-sh/uv:0.11.21@sha256:ff07b86af50d4d9391d9daf4ff89ce427bc544f9aae87057e69a1cc0aa369946 AS uv

# Stage 1: Build environment
FROM ubuntu:24.04 AS builder

# Build arguments for user permissions (host UID/GID compatibility)
ARG BUILD_UID=1000
ARG BUILD_GID=1000

# Build arguments for configurable builds (sanitizers, coverage)
ARG CMAKE_BUILD_TYPE=Release
ARG CMAKE_CXX_FLAGS=""
ARG ASAN_OPTIONS=""
ARG UBSAN_OPTIONS=""
ARG TSAN_OPTIONS=""
ARG MSAN_OPTIONS=""

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies. The C++ compiler (clang-18), libc++ dev headers,
# OpenSSL headers, lcov, and git come from apt; the CMake/Ninja/Conan/gcovr
# build toolchain is uv-managed as locked PyPI wheels (Odysseus ADR-018), not
# apt/pip. build-essential is retained for the system linker/headers.
RUN apt-get update && apt-get install -y \
    build-essential \
    clang-18 \
    clang++-18 \
    libc++-18-dev \
    libc++abi-18-dev \
    git \
    lcov \
    bc \
    libssl-dev \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100 \
    && rm -rf /var/lib/apt/lists/*

# uv binary (manages cmake/ninja/conan/gcovr as locked PyPI wheels).
COPY --from=uv /uv /uvx /usr/local/bin/

# uv toolchain environment configuration.
#
# The venv MUST live OUTSIDE /workspace: the `dev`/`build` compose services
# bind-mount the host repo over `.:/workspace:Z` at run time, which masks any
# `/workspace/.venv` baked into the image — so a default in-tree venv vanishes
# the moment the container starts. Installing it at /opt/venv keeps the locked
# build toolchain (conan/cmake/ninja/gcovr) present at run time regardless of
# the bind mount.
#
# Putting /opt/venv/bin on PATH makes the tools resolvable by their BARE names
# (`conan`, `cmake`, `ninja`, `gcovr`) — which is exactly how the Makefile's
# container recipes invoke them (`$(CONTAINER_PREFIX) conan ...`, Makefile:83).
# Before ADR-018 these came from apt (`cmake`) and a system pip `conan`, so
# they were on PATH; uv-managing them without this PATH entry is what broke
# `make deps` in the lint/coverage jobs with "conan: executable file not found"
# (exit 127). UV_PYTHON_INSTALL_DIR keeps uv's downloaded CPython out of
# /root/.local (mode 0700) so the non-root compose `user:` can reach it.
ENV UV_PROJECT_ENVIRONMENT=/opt/venv \
    UV_PYTHON_INSTALL_DIR=/opt/uv-python \
    PATH="/opt/venv/bin:$PATH"

# Create workspace directory with proper permissions
RUN mkdir -p /workspace && \
    chmod 755 /workspace

# Set working directory
WORKDIR /workspace

# Sync the locked build toolchain first so it caches independently of sources.
# Installs into /opt/venv (UV_PROJECT_ENVIRONMENT). world-readable/executable so
# the non-root compose `user:` (keep-id) can run the tools off PATH; the
# downloaded interpreter under /opt/uv-python likewise.
COPY pyproject.toml uv.lock .python-version ./
RUN uv sync --locked \
    && chmod -R a+rX /opt/venv /opt/uv-python

# Detect Conan profile and install dependencies
# Use Release build type to match Docker production build
RUN conan profile detect --force

# Verify C++20 support and that the uv-managed toolchain is on PATH by bare name.
RUN clang++ --version && cmake --version && conan --version

# Copy conanfile first for dependency layer caching
COPY conanfile.py ./

# Install Conan dependencies (cached layer — only rebuilds if conanfile.py changes)
RUN conan install . --output-folder=build/conan-deps --build=missing \
    -s build_type=Release -s compiler.cppstd=20

# Copy project files
COPY CMakeLists.txt ./
COPY LICENSE ./
COPY README.md ./
COPY cmake/ ./cmake/
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/
COPY benchmarks/ ./benchmarks/
COPY fuzz/ ./fuzz/

# Build the project using Conan toolchain
RUN cmake -S . -B build/release -G Ninja \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS}" \
        -DCMAKE_TOOLCHAIN_FILE=build/conan-deps/conan_toolchain.cmake \
    && cmake --build build/release

# Stage 2: Test runner (runs the built test suites)
FROM ubuntu:24.04 AS test

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy built test executables from builder.
# The agent-delegation/coordination e2e binaries (basic_delegation_tests,
# module_coordination_tests, component_coordination_tests) were removed when
# the agent hierarchy was extracted to ProjectAgamemnon (ADR-015), so they no
# longer build. Ship the transport/concurrency smoke tests instead — these
# exercise the pure-C++20 transport role Keystone now owns.
COPY --from=builder /workspace/build/release/bin/transport_unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/bridge_unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/concurrency_unit_tests /usr/local/bin/

# Set working directory
WORKDIR /app

# Default command: run the transport/concurrency smoke tests
CMD ["sh", "-c", "transport_unit_tests && bridge_unit_tests && concurrency_unit_tests"]

# Stage 3: Production environment (Kubernetes deployment)
# Ships the Keystone daemon service binary — NOT test executables.
# See issue #513: the previous version incorrectly packaged test binaries here.
FROM ubuntu:24.04 AS production

# Install runtime dependencies. wget is used for the healthcheck so that
# Python3 (a dev tool) is not required in the production image.
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN groupadd -g 1001 hmas || true && \
    useradd -r -u 1001 -g 1001 hmas && \
    mkdir -p /app && \
    chown -R hmas:hmas /app

# Copy only the production service binary from the builder stage.
# The keystone_server target sets OUTPUT_NAME "keystone-server" so CMake
# places it at build/release/bin/keystone-server.
COPY --from=builder /workspace/build/release/bin/keystone-server /usr/local/bin/keystone-server

# Set working directory
WORKDIR /app

# Switch to non-root user
USER hmas

# Expose ports
EXPOSE 8080 9090 50051

# Health check against the daemon's /v1/health endpoint (port 8080).
# Uses wget (available in the base image after the apt layer above) so that
# Python3 is not required in this image.
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD wget -qO- http://localhost:8080/v1/health || exit 1

# Default command: run the Keystone daemon
CMD ["/usr/local/bin/keystone-server"]

# Stage 4: Development environment (for iterative development)
FROM builder AS development

# Install additional development tools.
# rpm provides rpmbuild, required by CPack's RPM generator: the CI `package`
# job runs `cpack -G RPM` inside this container (the release tree is
# configured in-container, so cpack must run here too — see
# .github/workflows/_required.yml).
RUN apt-get update && apt-get install -y \
    gdb \
    valgrind \
    clang-format \
    clang-tidy \
    cppcheck \
    lcov \
    bc \
    rpm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Keep the build directory
# This stage is for development with mounted volumes

CMD ["/bin/bash"]
