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
