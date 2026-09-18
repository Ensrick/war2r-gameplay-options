#pragma once

namespace logx {

// Opens <dll dir>\autocast.log (truncating). Safe to call from DllMain: plain CreateFile, no CRT stdio.
void Open(const wchar_t* dllDir);
void Write(const char* fmt, ...);

}  // namespace logx
