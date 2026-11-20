#include "common/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace scmd {

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    ::OutputDebugStringW(buf);
    ::OutputDebugStringW(L"\n");
}

}  // namespace scmd
