// SPDX-License-Identifier: BSD-3-Clause
#include "core/subject_validator.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string input(reinterpret_cast<const char*>(data), size);
  try {
    keystone::core::validateSubjectToken(input, "fuzz_input");
  } catch (const std::invalid_argument&) {
    // Expected — invalid token correctly rejected
  }
  return 0;
}
