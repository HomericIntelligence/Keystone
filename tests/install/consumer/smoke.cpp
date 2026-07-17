// One-line TU proving an installed public header is consumable downstream.
// core/message.hpp is self-contained (std-only includes), so it must compile
// against the bare install prefix with no third-party include paths.
#include <core/message.hpp>

static_assert(sizeof(keystone::core::KeystoneMessage) > 0,
              "installed keystone public header must be consumable");
