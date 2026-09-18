#pragma once
#include <cstdint>

namespace mod {

void SetModuleBase(uintptr_t exeBase, const wchar_t* dllDir);
void __cdecl OnTick();      // runs on the game thread once per simulation step (hooked AI tick)
void RunAutocastPass();     // test entry: one autocast sweep right now, ignoring the interval

}  // namespace mod
