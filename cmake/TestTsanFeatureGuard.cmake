cmake_minimum_required(VERSION 3.20)

foreach(required_variable CXX_COMPILER KEYSTONE_SOURCE_DIR KEYSTONE_BINARY_DIR)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

set(fake_include_dir "${KEYSTONE_BINARY_DIR}/tsan-feature-guard/gtest")
file(MAKE_DIRECTORY "${fake_include_dir}")
file(WRITE "${fake_include_dir}/gtest.h" "#pragma once\n")
foreach(
  standard_header
  chrono
  map
  mutex
  optional
  shared_mutex
  string
  thread
  vector)
  file(WRITE "${KEYSTONE_BINARY_DIR}/tsan-feature-guard/${standard_header}"
       "#pragma once\n")
endforeach()

set(profiling_test "${KEYSTONE_SOURCE_DIR}/tests/unit/test_profiling.cpp")
set(timing_assertion
    "EXPECT_NEAR\\([ \t\r\n]*stats->p95_us,[ \t\r\n]*950\\.0,[ \t\r\n]*1200\\.0[ \t\r\n]*\\);"
)

function(check_tsan_guard case_name expect_timing_assertion)
  execute_process(
    COMMAND
      "${CXX_COMPILER}" -std=c++20 -E -P -nostdinc++ ${ARGN}
      "-I${KEYSTONE_BINARY_DIR}/tsan-feature-guard"
      "-I${KEYSTONE_SOURCE_DIR}/include" "${profiling_test}"
    RESULT_VARIABLE preprocess_result
    OUTPUT_VARIABLE preprocessed_source
    ERROR_VARIABLE preprocess_error)

  if(NOT preprocess_result EQUAL 0)
    message(
      FATAL_ERROR
        "${case_name}: preprocessing failed with exit ${preprocess_result}:\n${preprocess_error}"
    )
  endif()

  string(REGEX MATCH "${timing_assertion}" assertion_match
               "${preprocessed_source}")
  if(expect_timing_assertion AND assertion_match STREQUAL "")
    message(
      FATAL_ERROR "${case_name}: the non-TSan timing assertion was not compiled"
    )
  endif()
  if(NOT expect_timing_assertion AND NOT assertion_match STREQUAL "")
    message(
      FATAL_ERROR "${case_name}: the TSan build compiled the timing assertion")
  endif()

  message(STATUS "${case_name}: guard result is correct")
endfunction()

# Exercise the configured compiler's native ordinary and ThreadSanitizer feature
# signals before the controlled compatibility cases.
check_tsan_guard(configured-compiler-ordinary TRUE)
check_tsan_guard(configured-compiler-tsan FALSE -fsanitize=thread)

# GCC does not provide the function-like __has_feature macro. GCC defines
# __SANITIZE_THREAD__ when ThreadSanitizer instrumentation is active.
check_tsan_guard(gcc-style-ordinary TRUE -U__has_feature)
check_tsan_guard(gcc-style-tsan FALSE -U__has_feature -D__SANITIZE_THREAD__=1)

# Clang provides __has_feature. Define its two relevant results explicitly so
# this focused test does not depend on the sanitizer support of the host CPU.
check_tsan_guard(clang-style-ordinary TRUE -U__has_feature
                 "-D__has_feature(x)=0")
check_tsan_guard(clang-style-tsan FALSE -U__has_feature "-D__has_feature(x)=1")
