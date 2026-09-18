#pragma once
#include <cstdint>

namespace hook {

// Verifies the exe build and redirects the AI-tick call site to our stub. Returns false (and patches nothing)
// on any mismatch, so an updated game exe runs unmodified instead of crashing.
bool Install(uintptr_t exeBase);

}  // namespace hook
