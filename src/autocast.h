#pragma once
#include <cstdint>

namespace autocast {

void SetModuleBase(uintptr_t exeBase, const wchar_t* dllDir);
void __cdecl OnTick();  // runs on the game thread once per simulation step
void RunPass();         // one autocast sweep over the local player's casters (OnTick calls this on its interval)

}  // namespace autocast
