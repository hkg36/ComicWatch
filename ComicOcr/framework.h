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

void dbgprintf(const char* format, ...);
void dbgprintf(const wchar_t* format, ...);

constexpr UINT WMAPP_TRAYICON = WM_APP + 1;
constexpr UINT WM_USER_OCRFINISH = WM_USER + 1;
constexpr UINT WM_USER_TRANSFINISH = WM_USER + 2;
constexpr UINT HOTKEY_TOGGLE_WINDOW = 1;
constexpr UINT HOTKEY_REPLAY = 2;
constexpr UINT DRAG_CAPTURE_TIMER_ID = 1;
constexpr UINT START_TRANSLATE_DELAY_ID = 2;
constexpr UINT DRAG_CAPTURE_DELAY_MS = 500;
constexpr UINT START_TRANS_DELAY_MS = 500;
