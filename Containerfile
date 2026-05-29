# ProjectKeystone HMAS - C++20 Build Environment
# Multi-stage build for efficient image size

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

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    clang-18 \
    clang++-18 \
    libc++-18-dev \
    libc++abi-18-dev \
    git \
    ninja-build \
    lcov \
    bc \
    gcovr \
    python3-pip \
    libssl-dev \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100 \
    && pip install --no-cache-dir conan==2.0.0 --break-system-packages \
    && rm -rf /var/lib/apt/lists/*

# Detect Conan profile and install dependencies
# Use Release build type to match Docker production build
RUN conan profile detect --force

# Verify C++20 support
RUN clang++ --version && cmake --version

# Create workspace directory with proper permissions
RUN mkdir -p /workspace && \
    chmod 755 /workspace

# Set working directory
WORKDIR /workspace

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

# Stage 2: Runtime environment (smaller image)
# Ships transport and concurrency unit tests as a smoke-test image.
# Agent e2e tests (basic_delegation_tests etc.) are no longer built here;
# the agent layer now lives in ProjectAgamemnon per ADR-015.
FROM ubuntu:24.04 AS runtime

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy built transport and concurrency test executables from builder
COPY --from=builder /workspace/build/release/bin/transport_unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/concurrency_unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/simulation_unit_tests /usr/local/bin/

# Set working directory
WORKDIR /app

# Default command: run transport/concurrency smoke tests
CMD ["sh", "-c", "transport_unit_tests && concurrency_unit_tests && simulation_unit_tests"]

# Stage 3: Production environment (Kubernetes deployment)
# ProjectKeystone is a pure transport library — no standalone service binary exists.
# This stage ships library validation tests. A future keystone_daemon binary
# will replace this with a real service entrypoint when implemented.
FROM ubuntu:24.04 AS production

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN groupadd -g 1001 hmas || true && \
    useradd -r -u 1001 -g 1001 hmas && \
    mkdir -p /app && \
    chown -R hmas:hmas /app

# Copy library validation test executables from builder
COPY --from=builder /workspace/build/release/bin/unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/transport_unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/concurrency_unit_tests /usr/local/bin/
COPY --from=builder /workspace/build/release/bin/simulation_unit_tests /usr/local/bin/

# Set working directory
WORKDIR /app

# Switch to non-root user
USER hmas

# Expose ports (reserved for future keystone_daemon HTTP/metrics endpoints)
EXPOSE 8080 9090

# Health check — validate library integrity via unit tests
HEALTHCHECK --interval=60s --timeout=30s --start-period=10s --retries=1 \
    CMD unit_tests --gtest_filter="-*Integration*" || exit 1

# Default command: run library validation tests
CMD ["sh", "-c", "unit_tests && transport_unit_tests && concurrency_unit_tests"]

# Stage 4: Development environment (for iterative development)
FROM builder AS development

# Install additional development tools
RUN apt-get update && apt-get install -y \
    gdb \
    valgrind \
    clang-format \
    clang-tidy \
    cppcheck \
    lcov \
    bc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Keep the build directory
# This stage is for development with mounted volumes

CMD ["/bin/bash"]
