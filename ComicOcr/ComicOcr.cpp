// ComicOcr.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "ComicOcr.h"
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include "ScreenShot.h"
#include "BackProcess.h"
#include "../ComicWatch/MessageThread.h"

#define MAX_LOADSTRING 100

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

extern MessageThread workthread;
// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名
HWND g_hMainWnd = nullptr;
BOOL g_isWindowVisible = FALSE;
NOTIFYICONDATA g_trayIconData = {};
BOOL g_isDragging = FALSE;
BOOL g_hasDragRect = FALSE;
POINT g_dragStartPoint = {};
POINT g_dragCurrentPoint = {};

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

std::wstring ocr_result;
std::wstring trans_result;

BOOL AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ToggleMainWindow();
void ShowTrayMenu(HWND hWnd);
void Paint(HWND hWnd, HDC hdc);
RECT GetNormalizedDragRect();
void SaveDragRect();

bool SingleInstance()
{
	// 1. 创建一个唯一的 Mutex
	HANDLE hMutex = ::CreateMutex(NULL, FALSE, _T("Global\\ComicOcrSingleInstance"));
	// 2. 检查是否已经存在
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		if (hMutex)
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
	if (SingleInstance() == false) {
		return 0;
	}
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
	load_backprocess_config();

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
    wcex.hCursor        = LoadCursor(nullptr, IDC_CROSS);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm = wcex.hIcon;

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

   if (!RegisterHotKey(hWnd, HOTKEY_TOGGLE_WINDOW, MOD_ALT, 'Q')) // Alt+Q 切换窗口显示/隐藏
   {
	   std::cout << "Failed to register hotkey: " << GetLastError() << std::endl;
   }
   if (!RegisterHotKey(hWnd, HOTKEY_REPLAY, MOD_ALT, 'W')) // Alt+W 重新播放
   {
	   std::cout << "Failed to register hotkey: " << GetLastError() << std::endl;
   }

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
	case WM_CREATE:
		workthread.start();
		break;
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
			replay_sound();
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
	case WM_LBUTTONDOWN:
		if (g_isWindowVisible)
		{
			SetCapture(hWnd);
			g_isDragging = TRUE;
			g_hasDragRect = FALSE;
			g_dragStartPoint.x = GET_X_LPARAM(lParam);
			g_dragStartPoint.y = GET_Y_LPARAM(lParam);
			g_dragCurrentPoint = g_dragStartPoint;
		}
		return 0;
	case WM_MOUSEMOVE:
		if (g_isDragging)
		{
			g_dragCurrentPoint.x = GET_X_LPARAM(lParam);
			g_dragCurrentPoint.y = GET_Y_LPARAM(lParam);
			g_hasDragRect = TRUE;
			KillTimer(hWnd, DRAG_CAPTURE_TIMER_ID);
			KillTimer(hWnd, START_TRANSLATE_DELAY_ID);
			SetTimer(hWnd, DRAG_CAPTURE_TIMER_ID, DRAG_CAPTURE_DELAY_MS, nullptr);
			InvalidateRect(hWnd, nullptr, FALSE);
		}
		return 0;
	case WM_LBUTTONUP:
		if (g_isDragging)
		{
			KillTimer(hWnd, DRAG_CAPTURE_TIMER_ID);
			KillTimer(hWnd, START_TRANSLATE_DELAY_ID);
			ReleaseCapture();
			g_isDragging = FALSE;
			g_hasDragRect = FALSE;
			ToggleMainWindow();
		}
		return 0;
	case WM_TIMER:
		if (wParam == DRAG_CAPTURE_TIMER_ID && g_isDragging && g_hasDragRect)
		{
			KillTimer(hWnd, DRAG_CAPTURE_TIMER_ID);
			SaveDragRect();
		}
		if (wParam == START_TRANSLATE_DELAY_ID)
		{
			KillTimer(hWnd, START_TRANSLATE_DELAY_ID);
			if (!ocr_result.empty()) {
				start_translation(hWnd, ocr_result);
			}
		}
		return 0;
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc=BeginPaint(hWnd, &ps);
			Paint(hWnd, dc);
			EndPaint(hWnd, &ps);
		}
		break;
	case WM_DESTROY:
		KillTimer(hWnd, DRAG_CAPTURE_TIMER_ID);
		KillTimer(hWnd, START_TRANSLATE_DELAY_ID);
		if (GetCapture() == hWnd)
		{
			ReleaseCapture();
		}
		UnregisterHotKey(hWnd, HOTKEY_TOGGLE_WINDOW);
		UnregisterHotKey(hWnd, HOTKEY_REPLAY);
		RemoveTrayIcon();
		workthread.stop_and_join();
		PostQuitMessage(0);
		break;
	case WM_USER_OCRFINISH:
		{
		std::unique_ptr<std::wstring> msg(
			reinterpret_cast<std::wstring*>(lParam));
		ocr_result = std::move(*msg);
		auto cache_result = check_translation_cache(ocr_result);
		if (!cache_result.empty()) {
			trans_result = cache_result;
			dbgprintf(L"Translation cache hit for text: %s\n", ocr_result.c_str());
		} else {
			SetTimer(hWnd, START_TRANSLATE_DELAY_ID, START_TRANS_DELAY_MS, nullptr);
		}
		InvalidateRect(hWnd, nullptr, FALSE);
		}
		break;
	case WM_USER_TRANSFINISH: 
		{
		std::unique_ptr<std::wstring> msg(
			reinterpret_cast<std::wstring*>(lParam));
		trans_result = std::move(*msg);
		InvalidateRect(hWnd, nullptr, FALSE);
		}
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
Screenshot screenshot;
void ToggleMainWindow()
{
	if (!g_hMainWnd)
	{
		return;
	}

	if (g_isWindowVisible)
	{
		KillTimer(g_hMainWnd, DRAG_CAPTURE_TIMER_ID);
		KillTimer(g_hMainWnd, START_TRANSLATE_DELAY_ID);
		if (GetCapture() == g_hMainWnd)
		{
			ReleaseCapture();
		}
		g_isDragging = FALSE;
		g_hasDragRect = FALSE;
		if (!ocr_result.empty())
		{
			play_sound(ocr_result);
		}
		screenshot.release();
		ocr_result.clear();
		trans_result.clear();
		ShowWindow(g_hMainWnd, SW_HIDE);
		g_isWindowVisible = FALSE;
	}
	else
	{
		screenshot.getScreenshot(); // 切换到显示窗口时获取屏幕截图
		ocr_result.clear();
		trans_result.clear();
		g_isDragging = FALSE;
		g_hasDragRect = FALSE;
		ShowWindow(g_hMainWnd, SW_SHOW);
		SetForegroundWindow(g_hMainWnd);
		InvalidateRect(g_hMainWnd, nullptr, FALSE);
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

RECT GetNormalizedDragRect()
{
	RECT rect{};
	rect.left = std::min(g_dragStartPoint.x, g_dragCurrentPoint.x);
	rect.top = std::min(g_dragStartPoint.y, g_dragCurrentPoint.y);
	rect.right = std::max(g_dragStartPoint.x, g_dragCurrentPoint.x);
	rect.bottom = std::max(g_dragStartPoint.y, g_dragCurrentPoint.y);
	return rect;
}

void SaveDragRect()
{
	cv::Mat img = screenshot.getImgBuffer();
	if (img.empty())
	{
		return;
	}

	RECT rect = GetNormalizedDragRect();
	int x = std::max(0, static_cast<int>(rect.left));
	int y = std::max(0, static_cast<int>(rect.top));
	int right = std::min(img.cols, static_cast<int>(rect.right));
	int bottom = std::min(img.rows, static_cast<int>(rect.bottom));
	int width = right - x;
	int height = bottom - y;

	if (width <= 10 || height <= 10)
	{
		return;
	}

	cv::Mat cut = img(cv::Rect(x, y, width, height));
	//cv::imwrite("cut.webp", cut);
	ocr_image(g_hMainWnd, cut);
}

void Paint(HWND hWnd, HDC hdc)
{
	cv::Mat img = screenshot.getImgBuffer();
	if (img.empty())
	{
		return;
	}

	cv::Size imgSize = img.size();

	//截图绘制到窗口
	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP hBitmap = CreateCompatibleBitmap(hdc, imgSize.width, imgSize.height);
	HGDIOBJ oldBitmap = SelectObject(memDC, hBitmap);
	defer({
		SelectObject(memDC, oldBitmap);
		DeleteObject(hBitmap);
		DeleteDC(memDC);
		});

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = imgSize.width;
	bmi.bmiHeader.biHeight = -imgSize.height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	SetDIBitsToDevice(memDC,
		0, 0,
		imgSize.width, imgSize.height,
		0, 0,
		0,
		imgSize.height,
		img.data,
		&bmi,
		DIB_RGB_COLORS);

	if (g_isDragging && g_hasDragRect)
	{
		RECT rect = GetNormalizedDragRect();
		LOGBRUSH lb;
		lb.lbStyle = BS_SOLID;
		lb.lbColor = RGB(255, 0, 0);
		lb.lbHatch = 0;

		DWORD style[] = { 10, 7 };  // dash 和 gap 的长度（可自定义）

		HPEN pen = ExtCreatePen(
			PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT,
			2,                    // 宽度
			&lb,
			2,                    // style 数组长度
			style);               // dash pattern
		HGDIOBJ oldPen = SelectObject(memDC, pen);
		HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
		defer({
			SelectObject(memDC, oldBrush);
			SelectObject(memDC, oldPen);
			DeleteObject(pen);
			});
		Rectangle(memDC, rect.left, rect.top, rect.right, rect.bottom);

		HFONT hFont = CreateFontW(
			32, 0, 0, 0,
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
		
		if (!ocr_result.empty()) {
			SetBkMode(memDC, TRANSPARENT);
			SetTextColor(memDC, RGB(255, 255, 255));
			HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
			
			const int textX = rect.left;
			const int textY = rect.bottom+5;
			const int textMaxWidth = imgSize.width - rect.left;
			const int textMaxHeight = 300;

			RECT textRc{ textX, textY, textX + textMaxWidth, textY + textMaxHeight };
			int height = DrawTextW(memDC,
				ocr_result.c_str(),
				-1,
				&textRc,
				DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
			
			FillRect(memDC, &textRc, blackBrush);
			DrawTextW(memDC, ocr_result.c_str(), -1, &textRc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

			if (!trans_result.empty()) {
				RECT transTextRc{ textX, textRc.bottom + 5, textX + textMaxWidth, textRc.bottom + 5 + textMaxHeight };
				int transHeight = DrawTextW(memDC,
					trans_result.c_str(),
					-1,
					&transTextRc,
					DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);

				FillRect(memDC, &transTextRc, blackBrush);
				DrawTextW(memDC, trans_result.c_str(), -1, &transTextRc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
			}
		}
	}

	BitBlt(hdc, 0, 0, imgSize.width, imgSize.height, memDC, 0, 0, SRCCOPY);
}
