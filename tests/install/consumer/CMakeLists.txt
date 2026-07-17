# Minimal downstream consumer used by scripts/check-install.sh (issue #596).
# Proves the installed package is usable: find_package(Keystone) resolves, the
# imported target propagates include dirs, and a public header compiles.
#
# A STATIC library (not an executable) is deliberate: the install prefix ships
# only the keystone archives — the PUBLIC third-party deps (spdlog,
# concurrentqueue, nats.c) are not installed, so a full executable link is out
# of scope for this check.
cmake_minimum_required(VERSION 3.20)
project(keystone_install_smoke CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Keystone CONFIG REQUIRED)

add_library(keystone_consumer_smoke STATIC smoke.cpp)
target_link_libraries(keystone_consumer_smoke PRIVATE Keystone::keystone_core)
