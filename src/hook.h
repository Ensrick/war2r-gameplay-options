#pragma once
#include <cstdint>

namespace hook {

// Verifies the exe build and redirects the AI-tick call site to our stub. Returns false (and patches nothing)
// on any mismatch, so an updated game exe runs unmodified instead of crashing.
bool Install(uintptr_t exeBase);

// True when all three spell call sites of the computer's paladin AI still call what this build expects. Install()
// hooks them only then, and the selftest checks it against the real exe so a game patch fails loudly.
bool AiPaladinSitesMatch(uintptr_t exeBase);
// One of them: 0 heal, 1 exorcism while invisible, 2 exorcism. The selftest uses this to prove that Install patched
// every one of the three, not just the first.
bool AiPaladinSiteMatches(uintptr_t exeBase, int which);

}  // namespace hook
