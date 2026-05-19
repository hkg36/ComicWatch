#pragma once
class Screenshot
{
public:
    double static getZoom();
    cv::Mat getScreenshot();
    cv::Mat getScreenshot(int x, int y, int width, int height);
	cv::Mat getImgBuffer() const { return m_screenshot; }
	void release() { m_screenshot.release(); m_screenshotData.clear(); }
	~Screenshot() { release(); }
private:
    std::vector<char> m_screenshotData;
    cv::Mat m_screenshot;
};