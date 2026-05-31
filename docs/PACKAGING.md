# ProjectKeystone Packaging Guide

This document describes the CPack-based packaging system for ProjectKeystone HMAS.

## Package Overview

ProjectKeystone is distributed as five separate packages to support different use cases:

### 1. keystone (Runtime Package)

**Package Names:**

- DEB: `libkeystone0`
- RPM: `keystone`
- Archive: `ProjectKeystone-<version>-Linux-keystone.tar.gz`

**Contents:**

- Runtime shared libraries (`.so` files)
- Core documentation (README.md, LICENSE, CLAUDE.md)

**Target Users:** End users running applications built with ProjectKeystone

**Dependencies:**

- libc6 >= 2.34
- libstdc++6 >= 12

### 2. keystone-dev (Development Package)

**Package Names:**

- DEB: `libkeystone-dev`
- RPM: `keystone-devel`
- Archive: `ProjectKeystone-<version>-Linux-keystone-dev.tar.gz`

**Contents:**

- All public header files (`.hpp`)
- Static libraries (`.a` files)
- CMake package configuration files
  - `KeystoneConfig.cmake`
  - `KeystoneConfigVersion.cmake`
  - `KeystoneTargets.cmake`
- Proto files (if gRPC enabled)
- Build documentation (Dockerfile, docker-compose.yaml)

**Target Users:** Developers building applications with ProjectKeystone

**Dependencies:**

- keystone (runtime package)
- cmake >= 3.20

**Usage Example:**

```cmake
# In your CMakeLists.txt
find_package(Keystone REQUIRED COMPONENTS core concurrency agents)

add_executable(my_app main.cpp)
target_link_libraries(my_app
    Keystone::keystone_core
    Keystone::keystone_concurrency
    Keystone::keystone_agents
)
```

### 3. keystone-doc (Documentation Package)

**Package Names:**

- DEB: `keystone-doc`
- RPM: `keystone-doc`
- Archive: `ProjectKeystone-<version>-Linux-keystone-doc.tar.gz`

**Contents:**

- Complete documentation from `docs/` directory
  - Architecture guides (FOUR_LAYER_ARCHITECTURE.md, etc.)
  - Phase plans (PHASE_*.md)
  - API references
  - Deployment guides (KUBERNETES_DEPLOYMENT.md, etc.)

**Target Users:** Developers, architects, and technical writers

**Dependencies:** None (architecture-independent)

### 4. keystone-test (Test Package)

**Package Names:**

- DEB: `keystone-test`
- RPM: `keystone-test`
- Archive: `ProjectKeystone-<version>-Linux-keystone-test.tar.gz`

**Contents:**

- All test executables
  - Unit tests (`unit_tests`, `concurrency_unit_tests`, `simulation_unit_tests`)
  - E2E tests (`basic_delegation_tests`, `module_coordination_tests`, etc.)
  - gRPC tests (if enabled: `distributed_grpc_tests`)

**Target Users:** QA engineers, CI/CD systems, release validation

**Dependencies:**

- keystone (runtime package)

**Installation Location:**

- `/usr/bin/tests/` (system-wide)
- `bin/tests/` (local install)

**Usage Example:**

```bash
# Install test package
sudo apt install keystone-test

# Run all tests
/usr/bin/tests/unit_tests
/usr/bin/tests/basic_delegation_tests
/usr/bin/tests/module_coordination_tests

# Run with GTest filters
/usr/bin/tests/unit_tests --gtest_filter=MessageBus.*
```

### 5. keystone-misc (Tools Package)

**Package Names:**

- DEB: `keystone-tools`
- RPM: `keystone-tools`
- Archive: `ProjectKeystone-<version>-Linux-keystone-misc.tar.gz`

**Contents:**

- Benchmark executables
  - `message_pool_benchmarks`
  - `distributed_benchmarks`
- Load testing tools
  - `hmas_load_test`
- Fuzz testing targets (if enabled)
  - `fuzz_message_serialization`
  - `fuzz_message_bus_routing`
  - `fuzz_work_stealing`
  - `fuzz_retry_policy`

**Target Users:** Performance engineers, security researchers

**Dependencies:**

- keystone (runtime package)

**Installation Locations:**

- `/usr/bin/benchmarks/` - Benchmark tools
- `/usr/bin/tools/` - Load testing tools
- `/usr/bin/fuzz/` - Fuzz testing targets

## Building Packages

### Prerequisites

```bash
# Install CMake and build tools
sudo apt install cmake ninja-build

# Install packaging tools
sudo apt install dpkg rpm
```

### Basic Build

```bash
# Configure with CMake
mkdir -p build && cd build
cmake -G Ninja ..

# Build all targets
ninja

# Generate all packages
cpack
```

This generates:

- `ProjectKeystone-0.1.0-Linux-keystone.tar.gz`
- `ProjectKeystone-0.1.0-Linux-keystone-dev.tar.gz`
- `ProjectKeystone-0.1.0-Linux-keystone-doc.tar.gz`
- `ProjectKeystone-0.1.0-Linux-keystone-test.tar.gz`
- `ProjectKeystone-0.1.0-Linux-keystone-misc.tar.gz`
- `libkeystone0_0.1.0_amd64.deb` (and 4 more .deb files)
- `keystone-0.1.0-1.x86_64.rpm` (and 4 more .rpm files)

### Build with gRPC Support

```bash
mkdir -p build && cd build
cmake -G Ninja -DENABLE_GRPC=ON ..
ninja
cpack
```

This includes the `keystone_network` library and gRPC tests.

### Build Specific Package Format

```bash
# TGZ archives only
cpack -G TGZ

# DEB packages only
cpack -G DEB

# RPM packages only
cpack -G RPM

# ZIP archives only
cpack -G ZIP
```

### Build Specific Component

```bash
# Build only runtime package
cpack -G TGZ -D CPACK_COMPONENTS_ALL=keystone

# Build runtime + development
cpack -G DEB -D CPACK_COMPONENTS_ALL="keystone;keystone-dev"

# Build all except tests
cpack -G TGZ -D CPACK_COMPONENTS_ALL="keystone;keystone-dev;keystone-doc;keystone-misc"
```

### Docker Build

```bash
# Build packages in Docker (ensures reproducible builds)
docker build --target builder -t keystone-builder .
docker run --rm -v $(pwd)/packages:/out keystone-builder bash -c \
    "cd /workspace/build && cpack && cp *.tar.gz *.deb *.rpm /out/"
```

## Installing Packages

### Debian/Ubuntu (.deb)

```bash
# Install runtime only
sudo dpkg -i libkeystone0_0.1.0_amd64.deb

# Install development package
sudo dpkg -i libkeystone-dev_0.1.0_amd64.deb

# Install all packages
sudo dpkg -i *.deb

# Or use apt for dependency resolution
sudo apt install ./libkeystone0_0.1.0_amd64.deb
```

### RedHat/CentOS (.rpm)

```bash
# Install runtime only
sudo rpm -i keystone-0.1.0-1.x86_64.rpm

# Install development package
sudo rpm -i keystone-devel-0.1.0-1.x86_64.rpm

# Install all packages
sudo rpm -i *.rpm

# Or use yum/dnf for dependency resolution
sudo yum localinstall keystone-0.1.0-1.x86_64.rpm
```

### Archive (.tar.gz, .zip)

```bash
# Extract runtime package
tar -xzf ProjectKeystone-0.1.0-Linux-keystone.tar.gz -C /opt/keystone

# Set library path
export LD_LIBRARY_PATH=/opt/keystone/lib:$LD_LIBRARY_PATH

# Or install to system directories
sudo tar -xzf ProjectKeystone-0.1.0-Linux-keystone.tar.gz -C /usr/local
sudo ldconfig
```

## Uninstalling Packages

### Debian/Ubuntu

```bash
# Remove runtime package
sudo apt remove libkeystone0

# Remove all keystone packages
sudo apt remove 'libkeystone*' 'keystone-*'

# Purge (remove config files too)
sudo apt purge 'libkeystone*' 'keystone-*'
```

### RedHat/CentOS

```bash
# Remove runtime package
sudo rpm -e keystone

# Remove all keystone packages
sudo rpm -e keystone keystone-devel keystone-doc keystone-test keystone-tools
```

## Package Dependencies

### Dependency Graph

```
keystone (runtime)
    ├─── keystone-dev (depends on keystone)
    ├─── keystone-test (depends on keystone)
    └─── keystone-misc (depends on keystone)

keystone-doc (independent, no dependencies)
```

### Installing Minimal Set

For **end users** (just run applications):

```bash
sudo apt install libkeystone0
```

For **developers** (build applications):

```bash
sudo apt install libkeystone-dev  # Automatically installs libkeystone0
```

For **QA engineers** (run tests):

```bash
sudo apt install keystone-test  # Automatically installs libkeystone0
```

For **complete installation** (all packages):

```bash
sudo apt install libkeystone0 libkeystone-dev keystone-doc keystone-test keystone-tools
```

## Package Contents Reference

### keystone Runtime Package

```
/usr/lib/
├── libkeystone_core.so
├── libkeystone_concurrency.so
├── libkeystone_agents.so
├── libkeystone_simulation.so
└── libkeystone_network.so (if ENABLE_GRPC=ON)

/usr/share/doc/ProjectKeystone/
├── README.md
├── CLAUDE.md
└── LICENSE
```

### keystone-dev Development Package

```
/usr/include/keystone/
├── core/
│   ├── message.hpp
│   ├── message_bus.hpp
│   └── ...
├── agents/
│   ├── base_agent.hpp
│   ├── chief_architect_agent.hpp
│   └── ...
├── concurrency/
│   ├── task.hpp
│   ├── thread_pool.hpp
│   └── ...
├── simulation/
│   └── ...
└── network/ (if ENABLE_GRPC=ON)
    └── ...

/usr/lib/
├── libkeystone_core.a
├── libkeystone_concurrency.a
├── libkeystone_agents.a
└── libkeystone_simulation.a

/usr/lib/cmake/Keystone/
├── KeystoneConfig.cmake
├── KeystoneConfigVersion.cmake
└── KeystoneTargets.cmake

/usr/share/keystone/proto/ (if ENABLE_GRPC=ON)
├── hmas_coordinator.proto
├── service_registry.proto
└── common.proto

/usr/share/doc/ProjectKeystone/build/
├── Dockerfile
└── docker-compose.yaml
```

### keystone-doc Documentation Package

```
/usr/share/doc/ProjectKeystone/
├── MONITORING.md
├── KUBERNETES_DEPLOYMENT.md
├── PHASE_8_COMPLETE.md
├── plan/
│   ├── FOUR_LAYER_ARCHITECTURE.md
│   ├── TDD_FOUR_LAYER_ROADMAP.md
│   ├── PHASE_*_PLAN.md
│   └── ...
└── ...
```

### keystone-test Test Package

```
/usr/bin/tests/
├── basic_delegation_tests
├── module_coordination_tests
├── component_coordination_tests
├── async_delegation_tests
├── distributed_hierarchy_tests
├── distributed_grpc_tests (if ENABLE_GRPC=ON)
├── unit_tests
├── concurrency_unit_tests
└── simulation_unit_tests
```

### keystone-misc Tools Package

```
/usr/bin/benchmarks/
├── message_pool_benchmarks
└── distributed_benchmarks

/usr/bin/tools/
└── hmas_load_test

/usr/bin/fuzz/ (if ENABLE_FUZZING=ON)
├── fuzz_message_serialization
├── fuzz_message_bus_routing
├── fuzz_work_stealing
└── fuzz_retry_policy
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build and Package

on: [push, pull_request]

jobs:
  package:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y cmake ninja-build dpkg rpm

      - name: Build
        run: |
          mkdir -p build && cd build
          cmake -G Ninja ..
          ninja

      - name: Create packages
        run: |
          cd build
          cpack

      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: packages
          path: |
            build/*.tar.gz
            build/*.deb
            build/*.rpm
```

### GitLab CI Example

```yaml
package:
  stage: build
  image: ubuntu:22.04
  script:
    - apt update && apt install -y cmake ninja-build dpkg rpm
    - mkdir -p build && cd build
    - cmake -G Ninja ..
    - ninja
    - cpack
  artifacts:
    paths:
      - build/*.tar.gz
      - build/*.deb
      - build/*.rpm
    expire_in: 1 week
```

## Source Package

To create a source package (for distribution to other developers):

```bash
cd build
cpack --config CPackSourceConfig.cmake
```

This generates:

- `ProjectKeystone-0.1.0-Source.tar.gz`
- `ProjectKeystone-0.1.0-Source.zip`

Source packages exclude build artifacts and version control files.

## Version Management

Package version is defined in `CMakeLists.txt`:

```cmake
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "1")
set(CPACK_PACKAGE_VERSION_PATCH "0")
```

For releases, update these values and rebuild:

```bash
# Update version in CMakeLists.txt
sed -i 's/VERSION_PATCH "0"/VERSION_PATCH "1"/' CMakeLists.txt

# Rebuild packages
cd build
cmake ..
ninja
cpack
```

## Troubleshooting

### Missing LICENSE file

If LICENSE file is missing:

```bash
touch LICENSE
echo "BSD 3-Clause License" > LICENSE
cpack
```

### Package component not found

Ensure all targets are built before packaging:

```bash
ninja  # Build all targets first
cpack  # Then create packages
```

### DEB/RPM not generated on macOS

DEB and RPM generators only work on Linux. Use Docker:

```bash
docker run --rm -v $(pwd):/workspace -w /workspace ubuntu:22.04 bash -c \
    "apt update && apt install -y cmake ninja-build dpkg rpm && \
     mkdir -p build && cd build && cmake -G Ninja .. && ninja && cpack"
```

## References

- [CPack Documentation](https://cmake.org/cmake/help/latest/module/CPack.html)
- [ProjectKeystone Architecture](docs/plan/FOUR_LAYER_ARCHITECTURE.md)
- [Build System Guide](docs/plan/build-system.md)
