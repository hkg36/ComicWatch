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
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <opencv2/opencv.hpp>

class DeferHelper {
public:
    DeferHelper(std::function<void()> func) : func_(func) {}
    ~DeferHelper() { if (func_) func_(); }
private:
    std::function<void()> func_;
};

// 宏定义：利用 __LINE__ 生成唯一的变量名
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define defer(func) DeferHelper TOKENPASTE2(defer_dummy_, __LINE__)([&](){ func; })

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

constexpr UINT WMAPP_TRAYICON = WM_APP + 1;
constexpr UINT WM_USER_OCRFINISH = WM_USER + 1;
constexpr UINT WM_USER_TRANSFINISH = WM_USER + 2;
constexpr UINT HOTKEY_TOGGLE_WINDOW = 1;
constexpr UINT HOTKEY_REPLAY = 2;
constexpr UINT DRAG_CAPTURE_TIMER_ID = 1;
constexpr UINT START_TRANSLATE_TIMER_ID = 2;
constexpr UINT DRAG_CAPTURE_DELAY_MS = 300;
constexpr UINT START_TRANS_DELAY_MS = 600;
