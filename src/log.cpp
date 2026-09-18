#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace logx {

static HANDLE g_file = INVALID_HANDLE_VALUE;
static unsigned g_lines = 0;
constexpr unsigned kMaxLines = 20000;  // a runaway cast loop must not fill the disk

void Open(const wchar_t* dllDir) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\autocast.log", dllDir);
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Write(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE || g_lines >= kMaxLines) return;
    char buf[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = sprintf_s(buf, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    const int avail = static_cast<int>(sizeof(buf)) - n - 2;
    int m = vsnprintf(buf + n, avail, fmt, ap);
    va_end(ap);
    if (m < 0) m = 0;
    if (m > avail - 1) m = avail - 1;  // vsnprintf returns the untruncated length
    n += m;
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD written;
    WriteFile(g_file, buf, n, &written, nullptr);
    if (++g_lines == kMaxLines) {
        const char tail[] = "log line cap reached, logging stopped\r\n";
        WriteFile(g_file, tail, sizeof(tail) - 1, &written, nullptr);
    }
}

}  // namespace logx
