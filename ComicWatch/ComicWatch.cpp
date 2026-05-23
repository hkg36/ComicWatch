// ComicWatch.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "ComicWatch.h"
#include "MessageThread.h"

#include <filesystem>
#include <string>
#include <vector>
#include <cwchar>
#include <fkYAML/node.hpp>

#define MAX_LOADSTRING 100

extern MessageThread g_messageThread;
void _dbgprintf(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}
void _dbgprintf(const wchar_t* format, ...)
{
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    vswprintf(buffer, sizeof(buffer) / sizeof(wchar_t), format, args);
    va_end(args);
    OutputDebugStringW(buffer);
}


namespace fs = std::filesystem;

// 全局变量:
HINSTANCE hInst;                                // 当前实例
HWND mainWnd = NULL;                            // 主窗口句柄
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

constexpr UINT ID_POPUP_OPEN = 40001;
constexpr UINT ID_POPUP_RESTORE = 40002;
constexpr UINT ID_POPUP_EXIT = 40003;

struct AppState {
    int fileCount = 0;
    int fileIndex = -1;

    int imageCount = 0;
    int imageIndex = -1;
    cv::Mat currentImage;
    std::wstring statusMessage;
    std::wstring currentFolder;
	std::wstring currentZipPath;

    ULONGLONG wheelBurstStartTick = 0;
    ULONGLONG wheelLastFlipTick = 0;
    bool wasMaximized = false;
} g_state;
struct Config {
    std::wstring lastFolder;
	bool wasMaximized = false;
} old_config;

std::wstring g_yamlPath;

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                handle_mouse_wheel(HWND hWnd, int wheelDelta);
std::wstring        OpenFileDialog(HWND hWnd);

std::wstring get_yaml_path() {
    WCHAR exePath[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"ComicWatch.yaml";
    fs::path iniPath(exePath);
    iniPath.replace_extension(L".yaml");
    return iniPath.wstring();
}

std::wstring UTF8ToWString(const std::string& utf8_str)
{
    if (utf8_str.empty()) return {};

    int size_needed = MultiByteToWideChar(CP_UTF8, 0,
        utf8_str.c_str(),
        static_cast<int>(utf8_str.size()),
        nullptr, 0);
    if (size_needed <= 0) return {};
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0,
        utf8_str.c_str(),
        static_cast<int>(utf8_str.size()),
        &wstr[0], size_needed);

    return wstr;
}
std::string WStringToUTF8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
        static_cast<int>(wstr.size()),
        nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return {};

    std::string utf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
        static_cast<int>(wstr.size()),
        &utf8[0], size_needed, nullptr, nullptr);
    return utf8;
}
bool load_persisted_state() {
    g_yamlPath = get_yaml_path();

    fs::path path(g_yamlPath);
    std::ifstream file(path, std::ios::binary);

    // 2. 检查文件是否成功打开
    if (!file.is_open()) {
        std::wcerr << L"无法打开文件！" << std::endl;
        return false;
    }
    
    auto node = fkyaml::node::deserialize(file);
    file.close();

    auto &sessionNode = node["Session"];
    g_state.currentFolder = UTF8ToWString(sessionNode["CurrentFolder"].is_string() ? sessionNode["CurrentFolder"].get_value<std::string>() : "");
	old_config.lastFolder = g_state.currentFolder;
	auto& windowNode = node["Window"];
	g_state.wasMaximized = windowNode["isMaximized"].is_boolean() ? windowNode["isMaximized"].get_value<bool>() : false;
	old_config.wasMaximized = g_state.wasMaximized;

    return true;
}

bool save_persisted_state(HWND hWnd) {
    if (g_yamlPath.empty()) {
        g_yamlPath = get_yaml_path();
    }

    WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
    if (!GetWindowPlacement(hWnd, &placement)) {
        return false;
    }
	bool isMaximized = (placement.showCmd == SW_SHOWMAXIMIZED);
	if (old_config.lastFolder == g_state.currentFolder && old_config.wasMaximized == isMaximized) {
        return true;
    }
    fkyaml::node root = fkyaml::node::mapping();
	auto winNode = fkyaml::node::mapping();
	winNode["isMaximized"] = isMaximized;
    auto sessionNode= fkyaml::node::mapping();
    sessionNode["CurrentFolder"] = WStringToUTF8(g_state.currentFolder);
    root["Session"] = std::move(sessionNode);
    root["Window"] = std::move(winNode);

    fs::path path(g_yamlPath);
	std::ofstream ofs(path, std::ios::out | std::ios::binary);
	ofs << root;
    ofs.close();
    return true;
}

void apply_snapshot(WorkerSnapshot& snapshot) {
    g_state.fileCount = snapshot.fileCount;
    g_state.fileIndex = snapshot.fileIndex;
    g_state.imageCount = snapshot.imageCount;
    g_state.imageIndex = snapshot.imageIndex;
    g_state.currentImage = snapshot.currentImage;
    g_state.statusMessage = std::move(snapshot.statusMessage);
    g_state.currentFolder = std::move(snapshot.currentFolder);
	g_state.currentZipPath = std::move(snapshot.currentFilePath);
}

void reset_wheel_state() {
    g_state.wheelBurstStartTick = 0;
    g_state.wheelLastFlipTick = 0;
}

bool ui_open_by_dialog(HWND hWnd) {
    std::wstring selected = OpenFileDialog(hWnd);
    if (selected.empty()) return false;

	auto result = OpenPath(selected);
    if (result == nullptr) return false;
    bool ok = result->success;
    if (ok) {
		apply_snapshot(result->snapshot);
        InvalidateRect(hWnd, nullptr, FALSE);
        reset_wheel_state();
    }
    return ok;
}

bool ui_show_next_image(HWND hWnd) {
	auto result = ShowNextImage();
	if (result == nullptr) return false;
    bool ok = result->success;
    if (ok) {
		apply_snapshot(result->snapshot);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    return ok;
}

bool ui_show_prev_image(HWND hWnd) {
	auto result = ShowPrevImage();
	if (result == nullptr) return false;
    bool ok = result->success;
    if (ok) {
		apply_snapshot(result->snapshot);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    return ok;
}

void ui_show_next_zip(HWND hWnd) {
	auto result = ShowNextZip();
	if (result == nullptr) return;
	bool ok = result->success;
    if (ok) {
        apply_snapshot(result->snapshot);
        reset_wheel_state();
		InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ui_show_prev_zip(HWND hWnd) {
	auto result = ShowPrevZip();
	if (result == nullptr) return;
	bool ok = result->success;
    if (ok) {
        apply_snapshot(result->snapshot);
        reset_wheel_state();
		InvalidateRect(hWnd, nullptr, FALSE);
    }
}

void ui_delete_current_zip(HWND hWnd) {
	auto result = DeleteCurrentFile();
	if (result == nullptr) return;
	bool ok = result->success;
    if (ok) {
        apply_snapshot(result->snapshot);
        reset_wheel_state();
		InvalidateRect(hWnd, nullptr, FALSE);
	}
}

void update_window_border(HWND hWnd, bool borderless) {
    LONG style = GetWindowLong(hWnd, GWL_STYLE);
    if (borderless) {
        style &= ~WS_OVERLAPPEDWINDOW;
        style |= WS_POPUP;
    }
    else {
        style &= ~WS_POPUP;
        style |= WS_OVERLAPPEDWINDOW;
    }

    SetWindowLong(hWnd, GWL_STYLE, style);
}

void show_context_menu(HWND hWnd, int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenu(hMenu, MF_STRING, ID_POPUP_OPEN, _T("打开文件"));
    AppendMenu(hMenu, MF_STRING, ID_POPUP_RESTORE, _T("恢复正常窗口"));
    AppendMenu(hMenu, MF_STRING, ID_POPUP_EXIT, _T("退出"));

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case ID_POPUP_OPEN:
        ui_open_by_dialog(hWnd);
        break;
    case ID_POPUP_RESTORE:
        ShowWindow(hWnd, SW_RESTORE);
        break;
    case ID_POPUP_EXIT:
        DestroyWindow(hWnd);
        break;
    default:
        break;
    }
}

void handle_mouse_wheel(HWND hWnd, int wheelDelta) {
    dbgprintf("Wheel delta: %d\n", wheelDelta);
    if (wheelDelta == 0) return;

    int dir = (wheelDelta > 0) ? 1 : -1;
    ULONGLONG now = GetTickCount64();

    bool newBurst = false;
    if (g_state.wheelLastFlipTick != 0 && now - g_state.wheelLastFlipTick > 400) {
        newBurst = true;
    }

    if (newBurst || g_state.wheelBurstStartTick == 0) {
        g_state.wheelBurstStartTick = now;
        g_state.wheelLastFlipTick = 0;
    }

    ULONGLONG elapsed = now - g_state.wheelBurstStartTick;
    ULONGLONG minInterval = (elapsed < 1000) ? 200 : 100;

    if (g_state.wheelLastFlipTick != 0 && now - g_state.wheelLastFlipTick < minInterval) {
        return;
    }

    bool moved = false;
    if (dir > 0) {
        moved = ui_show_prev_image(mainWnd);
    }
    else {
        moved = ui_show_next_image(mainWnd);
    }

    if (!moved) {
        g_state.wheelBurstStartTick = 0;
        g_state.wheelLastFlipTick = 0;
        return;
    }
    now = GetTickCount64();
    g_state.wheelLastFlipTick = now;
}

std::wstring OpenFileDialog(HWND hWnd) {
    OPENFILENAME ofn{};
    WCHAR szFile[MAX_PATH]{};

    std::wstring initialDir = g_state.currentFolder;

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = _T("Support Files\0*.zip;*.jpg;*.jpeg;*.png;*.bmp;*.webp\0");
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return std::wstring();
}

bool SingleInstance()
{
    // 1. 创建一个唯一的 Mutex
    HANDLE hMutex = ::CreateMutex(NULL, FALSE, _T("Global\\ComicWatchSingleInstance"));
    // 2. 检查是否已经存在
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if(hMutex)
            CloseHandle(hMutex);
        return false; // 退出当前实例
    }
    return true;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
	if (SingleInstance() == false) {
        return 0;
    }
    g_state.statusMessage = L"请右键点击窗口选择打开文件";
    load_persisted_state();
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_COMICWATCH, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, g_state.wasMaximized ? SW_SHOWMAXIMIZED : nCmdShow)) {
        return FALSE;
    }
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_COMICWATCH));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = wcex.hIcon;

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    mainWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!mainWnd) {
        return FALSE;
    }

    ShowWindow(mainWnd, nCmdShow);
    UpdateWindow(mainWnd);

    return TRUE;
}

void paint_image(HWND hWnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hWnd, &rc);

    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientW, clientH);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
    defer({
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
		});

    HBRUSH whiteBrush = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    FillRect(memDC, &rc, whiteBrush);
    HFONT hFont = CreateFontW(
        26, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Microsoft YaHei"
    );

    HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
    defer({
        SelectObject(memDC, oldFont);
        DeleteObject(hFont);
    });

    if (g_state.currentImage.empty()) {
        if (!g_state.statusMessage.empty()) {
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(200, 30, 30));
            RECT textRc = rc;
            DrawTextW(memDC, g_state.statusMessage.c_str(), -1, &textRc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        }
    }
    else {
        cv::Mat bgra = g_state.currentImage;

        if (bgra.empty()) {
            return;
        }

        double sx = (double)clientW / bgra.cols;
        double sy = (double)clientH / bgra.rows;
        double scale = (sx < sy) ? sx : sy;
        int drawW = (int)(bgra.cols * scale);
        int drawH = (int)(bgra.rows * scale);
        if (drawW == 0 || drawH == 0) return;
        int x = (clientW - drawW) / 2;
        int y = (clientH - drawH) / 2;

        cv::Mat resized;
        int interp = (scale < 1.0) ? cv::INTER_AREA : cv::INTER_CUBIC;
        cv::resize(bgra, resized, cv::Size(drawW, drawH), 0.0, 0.0, interp);
        if (!resized.isContinuous()) {
            resized = resized.clone();
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = drawW;
        bmi.bmiHeader.biHeight = -drawH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetDIBitsToDevice(memDC,
            x, y,
            drawW, drawH,
            0, 0,
            0, drawH,
            resized.data,
            &bmi,
            DIB_RGB_COLORS);
    }

    if (g_state.imageCount > 0 && g_state.imageIndex >= 0 && g_state.imageIndex < g_state.imageCount) {
        //绘制一个进度条
		const int barWidth = 200;
		const int barHeight = 5;
        int bar_left = 15;
        int bar_top = 15;
        int bar_right = bar_left + barWidth;
        int bar_bottom = bar_top + barHeight;
		int proc_right = bar_left + (int)((bar_right - bar_left) * (g_state.imageIndex + 1) / (double)g_state.imageCount);
        HBRUSH hBrush = CreateSolidBrush(RGB(255, 0, 0)); // 创建红色画刷
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));  // 创建黑色实线画笔
        HPEN oldPen = (HPEN)SelectObject(memDC, hPen);
        Rectangle(memDC, bar_left, bar_top, bar_right, bar_bottom);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, hBrush);
		Rectangle(memDC, bar_left, bar_top, proc_right, bar_bottom);
        SelectObject(memDC, oldBrush);
        DeleteObject(hBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(hPen);
        
        auto zipFileName = std::wstring();
		if (!g_state.currentZipPath.empty())
            zipFileName = std::filesystem::path(g_state.currentZipPath).filename().wstring();
        std::wstring pageInfo = zipFileName;

        if (!pageInfo.empty()) {
            SetBkMode(memDC, TRANSPARENT);

			const int textX = 10;
			const int textY = bar_bottom + 5;
			const int textMaxWidth = 500;
			const int textMaxHeight = 300;
            RECT shadowRc{ textX + 1, textY + 1, textX +1+ textMaxWidth, textY +1+ textMaxHeight };
            SetTextColor(memDC, RGB(0, 0, 0));
            DrawTextW(memDC, pageInfo.c_str(), -1, &shadowRc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

            RECT textRc{ textX, textY, textX + textMaxWidth, textY + textMaxHeight };
            SetTextColor(memDC, RGB(255, 255, 255));
            DrawTextW(memDC, pageInfo.c_str(), -1, &textRc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
        }
    }

    BitBlt(hdc, 0, 0, clientW, clientH, memDC, 0, 0, SRCCOPY);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (wParam == SIZE_MAXIMIZED) {
            update_window_border(hWnd, true);
        }
        else if (wParam == SIZE_RESTORED) {
            update_window_border(hWnd, false);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        break;

    case WM_LBUTTONUP:
        if (g_state.fileCount == 0) {
            ui_open_by_dialog(hWnd);
        }
        break;

    case WM_RBUTTONUP:
    {
        POINT pt;
        GetCursorPos(&pt);
        show_context_menu(hWnd, pt.x, pt.y);
    }
    break;

    case WM_CONTEXTMENU:
    {
        int x = (int)(short)LOWORD(lParam);
        int y = (int)(short)HIWORD(lParam);
        if (x == -1 && y == -1) {
            POINT pt;
            GetCursorPos(&pt);
            x = pt.x;
            y = pt.y;
        }
        show_context_menu(hWnd, x, y);
    }
    break;

    case WM_MOUSEWHEEL:
    {
        int wheelDelta = (int)(short)HIWORD(wParam);
        handle_mouse_wheel(hWnd, wheelDelta);
    }
    break;

    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_LEFT:
        case VK_PRIOR:
            ui_show_prev_image(hWnd);
            break;
        case VK_RIGHT:
        case VK_NEXT:
            ui_show_next_image(hWnd);
            break;
        case VK_UP:
            ui_show_prev_zip(hWnd);
            break;
        case VK_DOWN:
            ui_show_next_zip(hWnd);
            break;
        case VK_DELETE:
            ui_delete_current_zip(hWnd);
            break;
        case VK_ESCAPE:
            DestroyWindow(hWnd);
            break;
        default:
            break;
        }
        break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case ID_POPUP_OPEN:
            ui_open_by_dialog(hWnd);
            break;
        case ID_POPUP_RESTORE:
            ShowWindow(hWnd, SW_RESTORE);
            break;
        case ID_POPUP_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        paint_image(hWnd, hdc);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_DESTROY:
        save_persisted_state(hWnd);
        g_messageThread.stop_and_join();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
