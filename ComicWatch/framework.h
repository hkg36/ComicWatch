// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <string>

inline void _dbgprintf(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}
inline void _dbgprintf(const wchar_t* format, ...)
{
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    vswprintf(buffer, sizeof(buffer) / sizeof(wchar_t), format, args);
    va_end(args);
    OutputDebugStringW(buffer);
}

template <typename... Args>
inline void dbgprintf_with_location(const char* file, int line, const char* format, Args... args)
{
    std::string fullFormat = "[%s:%d] ";
    fullFormat += format;
    _dbgprintf(fullFormat.c_str(), file, line, args...);
}

template <typename... Args>
inline void dbgprintf_with_location(const char* file, int line, const wchar_t* format, Args... args)
{
    std::wstring fullFormat = L"[%hs:%d] ";
    fullFormat += format;
    _dbgprintf(fullFormat.c_str(), file, line, args...);
}

#ifdef NDEBUG
#define dbgprintf(...)   ((void)0)
#else
#define dbgprintf(...)   dbgprintf_with_location(__FILE__, __LINE__, __VA_ARGS__)
#endif