#include "framework.h"
#include "ScreenShot.h"

#ifdef _DEBUG
#pragma comment(lib, "opencv_world4120d.lib")
#else
#pragma comment(lib, "opencv_world4120.lib")
#endif
/* 获取整个屏幕的截图 */
cv::Mat Screenshot::getScreenshot()
{
    int screen_width;
    int screen_height;
    HDC screenDC;
    HDC compatibleDC;
    HBITMAP hBitmap, hOldBitmap;

    if (!m_screenshot.empty())
        m_screenshot.release();

    //double zoom = getZoom();
    //m_width = GetSystemMetrics(SM_CXSCREEN) * zoom;
    //m_height = GetSystemMetrics(SM_CYSCREEN) * zoom;
    screen_width = GetSystemMetrics(SM_CXSCREEN);
    screen_height = GetSystemMetrics(SM_CYSCREEN);
    m_screenshotData.resize(screen_width * screen_height * 4);

    // 获取屏幕 DC
    screenDC = GetDC(NULL);
	defer({
		ReleaseDC(NULL, screenDC);
		});
    compatibleDC = CreateCompatibleDC(screenDC);
    defer({
        DeleteDC(compatibleDC); });

    // 创建位图
    hBitmap = CreateCompatibleBitmap(screenDC, screen_width, screen_height);
    hOldBitmap = (HBITMAP)SelectObject(compatibleDC, hBitmap);
    defer({
        SelectObject(compatibleDC, hOldBitmap);
        DeleteObject(hBitmap);
        });

    // 得到位图的数据
    BitBlt(compatibleDC, 0, 0, screen_width, screen_height, screenDC, 0, 0, SRCCOPY);
    GetBitmapBits(hBitmap, screen_width * screen_height * 4, m_screenshotData.data());

    // 创建图像
    m_screenshot = cv::Mat(screen_height, screen_width, CV_8UC4, m_screenshotData.data());

    return m_screenshot;
}

/** @brief 获取指定范围的屏幕截图
 * @param x 图像左上角的 X 坐标
 * @param y 图像左上角的 Y 坐标
 * @param width 图像宽度
 * @param height 图像高度
 */
cv::Mat Screenshot::getScreenshot(int x, int y, int width, int height)
{
    if (m_screenshot.empty())
        m_screenshot = getScreenshot();
    return m_screenshot(cv::Rect(x, y, width, height));
}

/* 获取屏幕缩放值 */
double Screenshot::getZoom()
{
    // 高DPI感知设置以后不需要这个函数
    // 获取窗口当前显示的监视器
    HWND hWnd = GetDesktopWindow();
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

    // 获取监视器逻辑宽度
    MONITORINFOEX monitorInfo;
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfo(hMonitor, &monitorInfo);
    int cxLogical = (monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left);

    // 获取监视器物理宽度
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    int cxPhysical = dm.dmPelsWidth;

    return cxPhysical * 1.0 / cxLogical;
}
