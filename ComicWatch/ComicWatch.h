#pragma once

#include "resource.h"

#include <opencv2/opencv.hpp>
#include <iostream>
#include <commdlg.h>
#include <string>
#include <vector>
#include <fstream>
#include <codecvt>

struct WorkerSnapshot {
    int fileCount = 0;
    int fileIndex = -1;
    int imageCount = 0;
    int imageIndex = -1;
    cv::Mat currentImage;
    std::wstring statusMessage;
    std::wstring currentFolder;
	std::wstring currentFilePath;
    WorkerSnapshot(WorkerSnapshot&& state) = default;
	WorkerSnapshot() = default;
    WorkerSnapshot& operator=(WorkerSnapshot&&) = default;
};

struct WorkerResult {
    WorkerSnapshot snapshot;
    bool success = false;
};


WorkerResult OpenPath(const std::wstring& zipPath);
WorkerResult ShowNextImage();
WorkerResult ShowPrevImage();
WorkerResult ShowNextZip();
WorkerResult ShowPrevZip();
WorkerResult DeleteCurrentFile();
WorkerResult GetSnapshot();

void dbgprintf(const char* format, ...);
void dbgprintf(const wchar_t* format, ...);

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