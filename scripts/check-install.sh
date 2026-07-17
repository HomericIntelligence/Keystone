#!/usr/bin/env bash
# Install smoke test for the canonical `install` CI check (issue #596).
#
# Runs INSIDE the dev container (paths in build/*/cmake_install.cmake are
# /workspace-absolute). Usage:
#   DOCKER_HOST= podman-compose exec -T dev ./scripts/check-install.sh
#
# 1. cmake --install the release tree into a clean staging prefix
# 2. Verify the declared install layout (libs, headers, CMake package config)
# 3. Configure + build a throwaway consumer that does
#    find_package(Keystone CONFIG REQUIRED) against the staged prefix
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build/x86.release}"
STAGING_REL="${STAGING_REL:-build/install-staging}"
STAGING="$(pwd)/${STAGING_REL}"
CONSUMER_BUILD="build/install-consumer"

if [ ! -f "${BUILD_DIR}/cmake_install.cmake" ]; then
  echo "ERROR: ${BUILD_DIR} is not a configured build tree (run 'make compile.release' first)"
  exit 1
fi

# --- 1. Install into a clean prefix -----------------------------------------
rm -rf "${STAGING}" "${CONSUMER_BUILD}"
cmake --install "${BUILD_DIR}" --prefix "${STAGING}"

# --- 2. Verify install layout ------------------------------------------------
fail=0
require() {
  if [ -e "${STAGING}/$1" ]; then
    echo "OK: $1"
  else
    echo "MISSING: ${STAGING_REL}/$1"
    fail=1
  fi
}

# Runtime/dev libraries (CMakeLists.txt install(TARGETS ...) — static archives)
require "lib/libkeystone_core.a"
require "lib/libkeystone_concurrency.a"
require "lib/libkeystone_simulation.a"

# CMake package config (install(FILES ...) to lib/cmake/Keystone)
require "lib/cmake/Keystone/KeystoneConfig.cmake"
require "lib/cmake/Keystone/KeystoneConfigVersion.cmake"

# Public headers: installed set must EXACTLY mirror the source include/ tree
# (both directions — a missing header and a stale extra file both fail).
if ! diff \
  <(cd include && find . -name '*.hpp' | sort) \
  <(cd "${STAGING}/include/keystone" && find . -name '*.hpp' | sort); then
  echo "ERROR: installed headers do not mirror source include/ tree"
  fail=1
else
  echo "OK: include/keystone header set mirrors source include/"
fi

if [ "${fail}" -ne 0 ]; then
  echo "ERROR: install layout verification failed"
  exit 1
fi

# --- 3. Consumer-compile smoke test ------------------------------------------
cmake -S tests/install/consumer -B "${CONSUMER_BUILD}" -G Ninja \
  -DCMAKE_PREFIX_PATH="${STAGING}"
cmake --build "${CONSUMER_BUILD}"

echo "OK: install smoke test passed (layout + find_package consumer compile)"
