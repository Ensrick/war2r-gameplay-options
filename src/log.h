#pragma once

namespace logx {

// Opens <dll dir>\gameplay_options.log for a new session; the logs of the last two sessions are kept next to it as
// gameplay_options.prev.log and .prev2.log. Safe to call from DllMain: plain CreateFile / MoveFileEx, no CRT stdio.
void Open(const wchar_t* dllDir);
void Write(const char* fmt, ...);

}  // namespace logx
