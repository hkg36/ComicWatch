// ComicOcr.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "ComicOcr.h"
#include <shellapi.h>

#define MAX_LOADSTRING 100

constexpr UINT WMAPP_TRAYICON = WM_APP + 1;
constexpr UINT HOTKEY_TOGGLE_WINDOW = 1;
constexpr UINT HOTKEY_REPLAY = 2;

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名
HWND g_hMainWnd = nullptr;
BOOL g_isWindowVisible = FALSE;
NOTIFYICONDATA g_trayIconData = {};

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

BOOL AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ToggleMainWindow();
void ShowTrayMenu(HWND hWnd);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 在此处放置代码。

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_COMICOCR, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_COMICOCR));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_COMICOCR));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   UNREFERENCED_PARAMETER(nCmdShow);
   hInst = hInstance;

   const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
   const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_POPUP,
      0, 0, screenWidth, screenHeight, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   g_hMainWnd = hWnd;
   g_isWindowVisible = FALSE;

   ShowWindow(hWnd, SW_HIDE);
   UpdateWindow(hWnd);

   if (!AddTrayIcon(hWnd))
   {
       DestroyWindow(hWnd);
       return FALSE;
   }

   if (!RegisterHotKey(hWnd, HOTKEY_TOGGLE_WINDOW, MOD_ALT, 'W')) // Alt+W 切换窗口显示/隐藏
   {
	   std::cout << "Failed to register hotkey: " << GetLastError() << std::endl;
   }
   if (!RegisterHotKey(hWnd, HOTKEY_REPLAY, MOD_ALT, 'Q')) // Alt+Q 重新播放
   {
	   std::cout << "Failed to register hotkey: " << GetLastError() << std::endl;
   }

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 分析菜单选择:
            switch (wmId)
            {
            case IDM_TRAY_TOGGLE:
                ToggleMainWindow();
                break;
            case IDM_TRAY_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_HOTKEY:
        switch(wParam)
        {
        case HOTKEY_TOGGLE_WINDOW:
            ToggleMainWindow();
            return 0;
        case HOTKEY_REPLAY:
            // TODO: 处理重新播放的逻辑
            return 0;
        }
        break;
    case WMAPP_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            ShowTrayMenu(hWnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            ToggleMainWindow();
        }
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        UnregisterHotKey(hWnd, HOTKEY_TOGGLE_WINDOW);
		UnregisterHotKey(hWnd, HOTKEY_REPLAY);
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

BOOL AddTrayIcon(HWND hWnd)
{
    g_trayIconData = {};
    g_trayIconData.cbSize = sizeof(NOTIFYICONDATA);
    g_trayIconData.hWnd = hWnd;
    g_trayIconData.uID = 1;
    g_trayIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIconData.uCallbackMessage = WMAPP_TRAYICON;
    g_trayIconData.hIcon = static_cast<HICON>(LoadImage(hInst, MAKEINTRESOURCE(IDI_COMICOCR), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    lstrcpyn(g_trayIconData.szTip, szTitle, ARRAYSIZE(g_trayIconData.szTip));

    return Shell_NotifyIcon(NIM_ADD, &g_trayIconData);
}

void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_trayIconData);
    if (g_trayIconData.hIcon)
    {
        DestroyIcon(g_trayIconData.hIcon);
        g_trayIconData.hIcon = nullptr;
    }
}

void ToggleMainWindow()
{
    if (!g_hMainWnd)
    {
        return;
    }

    if (g_isWindowVisible)
    {
        ShowWindow(g_hMainWnd, SW_HIDE);
        g_isWindowVisible = FALSE;
    }
    else
    {
        ShowWindow(g_hMainWnd, SW_SHOW);
        SetForegroundWindow(g_hMainWnd);
        g_isWindowVisible = TRUE;
    }
}

void ShowTrayMenu(HWND hWnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
    {
        return;
    }

    AppendMenu(hMenu, MF_STRING, IDM_TRAY_TOGGLE, g_isWindowVisible ? L"隐藏窗口" : L"显示窗口");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);//TrackPopupMenu 有时会出现菜单弹出后无法正常响应点击,把主程序窗口强制设为前景窗口，解决焦点和激活问题
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
    PostMessage(hWnd, WM_NULL, 0, 0);//TrackPopupMenu 是模态的，它会阻塞直到菜单消失。但有时菜单消失后，窗口的激活状态或消息队列会有点“卡住”。发一个 WM_NULL（空消息）可以让消息循环继续运转

    DestroyMenu(hMenu);
}
