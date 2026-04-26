message(STATUS "Conan: Using CMakeDeps conandeps_legacy.cmake aggregator via include()")
message(STATUS "Conan: It is recommended to use explicit find_package() per dependency instead")

find_package(spdlog)
find_package(concurrentqueue)
find_package(cnats)
find_package(GTest)
find_package(benchmark)

set(CONANDEPS_LEGACY  spdlog::spdlog  concurrentqueue::concurrentqueue  cnats::nats_static  gtest::gtest  benchmark::benchmark_main )