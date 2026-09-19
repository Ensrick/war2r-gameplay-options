#pragma once
#include <cstdint>

namespace mod {

void SetModuleBase(uintptr_t exeBase, const wchar_t* dllDir);
void EnsureConfigLoaded();  // first caller loads gameplay_options.toml (the map-load hook runs before the first tick)
void __cdecl OnTick();      // runs on the game thread once per simulation step (hooked AI tick)
void RunAutocastPass();     // test entry: one autocast sweep right now, ignoring the interval
// The hotkeys (Ctrl + [general] toggle_key, Ctrl + [auto_production] toggle_key) read key states through this;
// tests swap in a fake, nullptr puts the real GetAsyncKeyState + focus check back.
using KeyReader = bool (*)(int vk);
void SetKeyReaderForTest(KeyReader reader);

}  // namespace mod
