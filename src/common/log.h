#pragma once

namespace scmd {

// 输出到调试器（DebugView / VS 输出窗口），Debug 与 Release 都保留
void Log(const wchar_t* fmt, ...);

}  // namespace scmd
