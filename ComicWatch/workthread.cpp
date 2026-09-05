#include "framework.h"
#include "ComicWatch.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <cstdint>
#include <shellapi.h>
#include <windows.h>
#include <cwctype>

#define BIT7Z_AUTO_FORMAT
#include <bit7z/bit7z.hpp>
#include <bit7z/bitarchivereader.hpp>

#ifdef _DEBUG
#pragma comment(lib, "opencv_core4d.lib")
#pragma comment(lib, "opencv_imgcodecs4d.lib")
#pragma comment(lib, "opencv_imgproc4d.lib")
#pragma comment(lib,"bit7z64_d.lib")
#else
#pragma comment(lib, "opencv_core4.lib")
#pragma comment(lib, "opencv_imgcodecs4.lib")
#pragma comment(lib, "opencv_imgproc4.lib")
#pragma comment(lib,"bit7z64.lib")
#endif
#include "MessageThread.h"

MessageThread g_messageThread;
namespace fs = std::filesystem;
std::string utf8path(const std::wstring& wpath);

struct WorkerState {
	std::vector<std::wstring> zipFiles;
	std::wstring currentFolder;
	int zipIndex = -1;

	std::vector<std::string> imageEntries;
	std::map<std::string, std::uint32_t> imageEntryIndexMap;
	int imageIndex = -1;
	cv::Mat currentImage;
	std::wstring statusMessage;
	std::set<std::wstring> badImageEntries;

	std::map<int, cv::Mat> preloadCache;

	std::unique_ptr<bit7z::BitArchiveReader> archiveReader;
	bool archiveOpened = false;
	std::wstring archiveZipPath;
} g_worker;

struct FileWorkerState {
	std::wstring currentFolder;
	int zipIndex = -1;
	std::vector<std::wstring> imageFiles;
	int imageIndex = -1;
	cv::Mat currentImage;
	std::wstring statusMessage;
	std::set<std::wstring> badImageEntries;
	std::map<int, cv::Mat> preloadCache;
	bool folderOpened = false;
} g_fileWorker;
enum FileMode{
	FileMode_None,
	FileMode_Zip,
	FileMode_Folder
};
FileMode mode = FileMode_None;

const bit7z::Bit7zLibrary& get_bit7z_library() {
    // Allocate on heap to avoid static destruction order issues when
    // g_worker.archiveReader (a global) is destroyed after the library.
    static const bit7z::Bit7zLibrary* library = new bit7z::Bit7zLibrary(BIT7Z_STRING("7zip.dll"));
    return *library;
}

static std::string format_windows_error(DWORD err = ::GetLastError()) {
    if (err == 0) return std::string();
    LPSTR messageBuffer = nullptr;
    DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
    std::string message;
    if (size && messageBuffer) {
        message.assign(messageBuffer, size);
        LocalFree(messageBuffer);
    }
    return message;
}

const bit7z::BitInFormat& get_archive_format(const std::wstring& archivePath) {
    std::wstring ext = fs::path(archivePath).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    if (ext == L".rar") {
        return bit7z::BitFormat::Rar;
    }
    return bit7z::BitFormat::Zip;
}

std::string archive_item_path_utf8(const bit7z::BitArchiveItem& item) {
#ifdef _WIN32
	return utf8path(item.nativePath());
#else
	return item.nativePath();
#endif
}

void close_open_archive() {
	if (g_worker.archiveOpened) {
		g_worker.currentImage.release();
		g_worker.archiveReader.reset();
		g_worker.archiveOpened = false;
		g_worker.archiveZipPath.clear();
		g_worker.imageEntryIndexMap.clear();
		g_worker.badImageEntries.clear();
		g_worker.preloadCache.clear();
	}
	if (g_fileWorker.folderOpened){
		g_fileWorker.folderOpened = false;
		g_fileWorker.currentImage.release();
		g_fileWorker.badImageEntries.clear();
		g_fileWorker.preloadCache.clear();
	}
}

bool open_archive_for_zip(const std::wstring& zipPath) {
	if (g_worker.archiveOpened && g_worker.archiveZipPath == zipPath && g_worker.archiveReader) {
		return true;
	}

	close_open_archive();
	try {
        const auto& library = get_bit7z_library();
        const auto& format = bit7z::BitFormat::Auto;//get_archive_format(zipPath);
        g_worker.archiveReader = std::make_unique<bit7z::BitArchiveReader>(library, bit7z::to_tstring(zipPath), format);
    }
    catch (const bit7z::BitException& ex) {
        const char* what = ex.what();
        if (what && what[0]) {
            dbgprintf("Open archive failed: %ls, reason: %s\n", zipPath.c_str(), what);
        }
        else {
            DWORD err = ::GetLastError();
            std::string sys = format_windows_error(err);
            dbgprintf("Open archive failed: %ls, reason: (empty) GetLastError=%u, msg=%s\n", zipPath.c_str(), err, sys.c_str());
        }
        g_worker.archiveReader.reset();
        return false;
    }

	g_worker.archiveOpened = true;
	g_worker.archiveZipPath = zipPath;
	return true;
}

std::string utf8path(const std::wstring& wpath) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (size_needed <= 0) {
        size_needed = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (size_needed <= 0) return std::wstring();
        std::wstring out(size_needed, 0);
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], size_needed);
        return out;
    }

    std::wstring out(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size_needed);
    return out;
}

std::wstring make_bad_image_key(const std::wstring& zipPath, const std::string& entryName) {
    return zipPath + L"|" + utf8_to_wstring(entryName);
}

template <typename T>
std::basic_string<T> string_lower(std::basic_string<T> s) {
    std::transform(s.begin(), s.end(), s.begin(), [](T c) { return (T)towlower(c); });
    return s;
}

bool is_supported_image_file(const std::string& name) {
    static const char* support_ext[] = { ".jpg", ".png", ".bmp", ".jpeg", ".webp" };
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = string_lower(name.substr(dot));
    for (const auto* s : support_ext) {
        if (ext == s) return true;
    }
    return false;
}
bool is_supported_image_file(const std::wstring& name) {
    static const wchar_t* support_ext[] = { L".jpg", L".png", L".bmp", L".jpeg", L".webp" };
    auto dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = string_lower(name.substr(dot));
    for (const auto* s : support_ext) {
        if (ext == s) return true;
    }
    return false;
}

bool is_supported_archive_file(const std::wstring& name) {
    static const wchar_t* support_ext[] = { L".zip", L".rar" };
    auto dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = string_lower(name.substr(dot));
    for (const auto* s : support_ext) {
        if (ext == s) return true;
    }
    return false;
}

bool enumerate_image_entries_in_archive(std::vector<std::string>& entries) {
    entries.clear();
    g_worker.imageEntryIndexMap.clear();

    if (!g_worker.archiveOpened || !g_worker.archiveReader) {
        return false;
    }

    try {
        for (const auto& item : *g_worker.archiveReader) {
            if (item.isDir()) {
                continue;
            }

            std::string filename = archive_item_path_utf8(item);
            if (!filename.empty() && is_supported_image_file(filename)) {
                entries.push_back(filename);
                g_worker.imageEntryIndexMap[filename] = static_cast<std::uint32_t>(item.index());
            }
        }
        return true;
    }
    catch (const bit7z::BitException& ex) {
        const char* what = ex.what();
        if (what && what[0]) {
            dbgprintf("Enumerate archive entries failed: %ls, reason: %s\n", g_worker.archiveZipPath.c_str(), what);
        }
        else {
            DWORD err = ::GetLastError();
            std::string sys = format_windows_error(err);
            dbgprintf("Enumerate archive entries failed: %ls, reason: (empty) GetLastError=%u, msg=%s\n", g_worker.archiveZipPath.c_str(), err, sys.c_str());
        }
        entries.clear();
        g_worker.imageEntryIndexMap.clear();
        return false;
    }
}

bool read_image_entry_from_archive(const std::string& entryName, std::vector<unsigned char>& buffer) {
    buffer.clear();

    if (!g_worker.archiveOpened || !g_worker.archiveReader) {
        return false;
    }

    auto it = g_worker.imageEntryIndexMap.find(entryName);
    if (it == g_worker.imageEntryIndexMap.end()) {
        return false;
    }

    try {
        bit7z::buffer_t extracted;
        g_worker.archiveReader->extractTo(extracted, it->second);
        if (extracted.empty()) {
            return false;
        }
		buffer = std::move(extracted);
        return true;
    }
    catch (const bit7z::BitException& ex) {
        const char* what = ex.what();
        if (what && what[0]) {
            dbgprintf("Extract archive entry failed: %s, reason: %s\n", entryName.c_str(), what);
        }
        else {
            DWORD err = ::GetLastError();
            std::string sys = format_windows_error(err);
            dbgprintf("Extract archive entry failed: %s, reason: (empty) GetLastError=%u, msg=%s\n", entryName.c_str(), err, sys.c_str());
        }
        return false;
    }
}

template <typename T>
bool natural_less_icase(const std::basic_string<T>& a, const std::basic_string<T>& b)
{
    using CharT = T;
    size_t i = 0, j = 0;
    const size_t alen = a.length();
    const size_t blen = b.length();

    while (i < alen && j < blen)
    {
        if (!std::isdigit(static_cast<unsigned char>(a[i])) &&
            !std::iswdigit(static_cast<wint_t>(a[i]))
            )
        {
            CharT ca = static_cast<CharT>(std::tolower(static_cast<unsigned char>(a[i])));
            CharT cb = static_cast<CharT>(std::tolower(static_cast<unsigned char>(b[j])));

            if (ca != cb)
                return ca < cb;

            ++i;
            ++j;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(a[i])) ||
            std::iswdigit(static_cast<wint_t>(a[i]))
            )
        {
            if (!(std::isdigit(static_cast<unsigned char>(b[j])) ||
                std::iswdigit(static_cast<wint_t>(b[j]))))
                return false;

            unsigned long long numA = 0, numB = 0;

            while (i < alen && (std::isdigit(static_cast<unsigned char>(a[i])) ||
                std::iswdigit(static_cast<wint_t>(a[i]))))
            {
                numA = numA * 10 + (a[i] - '0');
                ++i;
            }

            while (j < blen && (std::isdigit(static_cast<unsigned char>(b[j])) ||
                std::iswdigit(static_cast<wint_t>(b[j]))))
            {
                numB = numB * 10 + (b[j] - '0');
                ++j;
            }

            if (numA != numB)
                return numA < numB;

            continue;
        }

        ++i;
        ++j;
    }

    return i == alen && j != blen;
}

void fill_snapshot(WorkerSnapshot& snapshot) {
    if (mode == FileMode_Zip) {
        snapshot.fileCount = (int)g_worker.zipFiles.size();
        snapshot.fileIndex = g_worker.zipIndex;
        snapshot.imageCount = (int)g_worker.imageEntries.size();
        snapshot.imageIndex = g_worker.imageIndex;
        snapshot.currentImage = g_worker.currentImage;
        snapshot.statusMessage = g_worker.statusMessage;
        snapshot.currentFolder = g_worker.currentFolder;
        snapshot.currentFilePath = (g_worker.zipIndex >= 0 && g_worker.zipIndex < (int)g_worker.zipFiles.size()) ? g_worker.zipFiles[g_worker.zipIndex] : L"";
    }
    else if (mode == FileMode_Folder) {
		snapshot.fileCount = (int)g_fileWorker.imageFiles.size();
        snapshot.fileIndex = g_fileWorker.imageIndex;
        snapshot.imageCount = (int)g_fileWorker.imageFiles.size();
        snapshot.imageIndex = g_fileWorker.imageIndex;
        snapshot.currentImage = g_fileWorker.currentImage;
        snapshot.statusMessage = g_fileWorker.statusMessage;
        snapshot.currentFolder = g_fileWorker.currentFolder;
        snapshot.currentFilePath = (g_fileWorker.imageIndex >= 0 && g_fileWorker.imageIndex < (int)g_fileWorker.imageFiles.size()) ? g_fileWorker.imageFiles[g_fileWorker.imageIndex] : L"";
    }
}

void scan_zip_files_in_folder(const std::wstring& folder) {
	assert(mode == FileMode_Zip);
	g_worker.zipFiles.clear();
	g_worker.currentFolder = folder;

	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(folder, ec)) {
		if (ec) break;
		if (!entry.is_regular_file()) continue;
		if (is_supported_archive_file(entry.path().filename().wstring())) {
			g_worker.zipFiles.push_back(entry.path().wstring());
		}
	}

	std::sort(g_worker.zipFiles.begin(), g_worker.zipFiles.end(),
		[](const std::wstring& a, const std::wstring& b) { return natural_less_icase(a, b); });
}
void scan_images_in_folder(const std::wstring& folder)
{
	assert(mode == FileMode_Folder);
    g_fileWorker.imageFiles.clear();
	g_fileWorker.currentFolder = folder;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(folder, ec))
    {
        if (ec)
        {
            break;
        }

        if (!entry.is_regular_file())
            continue;
        auto relative_path = entry.path().wstring();
        auto filename = entry.path().filename().wstring();

        if (is_supported_image_file(filename))
        {
            g_fileWorker.imageFiles.push_back(relative_path);
        }
    }
    std::sort(g_fileWorker.imageFiles.begin(), g_fileWorker.imageFiles.end(),
		[](const std::wstring& a, const std::wstring& b) { return natural_less_icase(a, b); });
}

bool decode_image_from_zip_entry(const std::wstring& zipPath, const std::string& entryName, cv::Mat& outImage, std::wstring* outStatus = nullptr) {
	assert(mode == FileMode_Zip);
	std::wstring badKey = make_bad_image_key(zipPath, entryName);
	if (g_worker.badImageEntries.find(badKey) != g_worker.badImageEntries.end()) {
		return false;
	}

	if (!open_archive_for_zip(zipPath)) {
		g_worker.badImageEntries.insert(std::move(badKey));
		return false;
	}
	dbgprintf("Decoding image from archive: %s, entry: %s\n", zipPath.c_str(), entryName.c_str());

	std::vector<unsigned char> buffer;
	if (!read_image_entry_from_archive(entryName, buffer) || buffer.empty()) {
		g_worker.badImageEntries.insert(std::move(badKey));
		return false;
	}

	dbgprintf("Read %zu bytes from archive entry: %s\n", buffer.size(), entryName.c_str());
	cv::Mat img = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
	if (img.empty()) {
		g_worker.badImageEntries.insert(std::move(badKey));
		if (outStatus) {
			*outStatus = L"图片解码失败(已加入损坏列表):\n" + utf8_to_wstring(entryName) + L"\n压缩包: " + zipPath;
		}
		return false;
	}

	cv::Mat img8;
	if (img.depth() == CV_8U) {
		img8 = img;
	}
	else if (img.depth() == CV_16U) {
		img.convertTo(img8, CV_MAKETYPE(CV_8U, img.channels()), 1.0 / 256.0);
	}
	else {
		cv::Mat tmp;
		double minV = 0.0, maxV = 0.0;
		cv::minMaxLoc(img.reshape(1), &minV, &maxV);
		if (maxV > minV) {
			img.convertTo(tmp, CV_MAKETYPE(CV_8U, img.channels()), 255.0 / (maxV - minV), -minV * 255.0 / (maxV - minV));
		}
		else {
			img.convertTo(tmp, CV_MAKETYPE(CV_8U, img.channels()));
		}
		img8 = tmp;
	}

	cv::Mat bgra;
	if (img8.channels() == 4) {
		bgra = img8;
	}
	else if (img8.channels() == 3) {
		cv::cvtColor(img8, bgra, cv::COLOR_BGR2BGRA);
	}
	else if (img8.channels() == 1) {
		cv::cvtColor(img8, bgra, cv::COLOR_GRAY2BGRA);
	}
	else {
		g_worker.badImageEntries.insert(std::move(badKey));
		if (outStatus) {
			*outStatus = L"图片通道数不受支持(已加入损坏列表):\n" + utf8_to_wstring(entryName) + L"\n压缩包: " + zipPath;
		}
		return false;
	}
	dbgprintf("Decoded image from archive: %s, size: %d x %d, channels: %d\n", entryName.c_str(), bgra.cols, bgra.rows, bgra.channels());
	outImage = bgra;
	return true;
}
bool decode_imagefile(const std::wstring& filePath, cv::Mat& outImage, std::wstring* outStatus = nullptr) {
	assert(mode == FileMode_Folder);
    std::wstring badKey = filePath;
    if (g_fileWorker.badImageEntries.find(badKey) != g_fileWorker.badImageEntries.end()) {
        return false;
    }
    cv::Mat img;
    std::vector<uchar> buffer;
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        auto size = file.tellg();
        if (size <= 0) {
            // 文件为空
            return false;
        }

        buffer.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(buffer.data()), size);
    }

    // 使用 imdecode 解析内存中的数据
    img = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);

    if (img.empty()) {
        g_fileWorker.badImageEntries.insert(std::move(badKey));
        if (outStatus) {
            *outStatus = L"图片解码失败(已加入损坏列表):\n" + filePath;
        }
        return false;
    }
    cv::Mat img8;
    if (img.depth() == CV_8U) {
        img8 = img;
    }
    else if (img.depth() == CV_16U) {
        img.convertTo(img8, CV_MAKETYPE(CV_8U, img.channels()), 1.0 / 256.0);
    }
    else {
        cv::Mat tmp;
        double minV = 0.0, maxV = 0.0;
        cv::minMaxLoc(img.reshape(1), &minV, &maxV);
        if (maxV > minV) {
            img.convertTo(tmp, CV_MAKETYPE(CV_8U, img.channels()), 255.0 / (maxV - minV), -minV * 255.0 / (maxV - minV));
        }
        else {
            img.convertTo(tmp, CV_MAKETYPE(CV_8U, img.channels()));
        }
        img8 = tmp;
    }
    cv::Mat bgra;
    if (img8.channels() == 4) {
        bgra = img8;
    }
    else if (img8.channels() == 3) {
        cv::cvtColor(img8, bgra, cv::COLOR_BGR2BGRA);
    }
    else if (img8.channels() == 1) {
        cv::cvtColor(img8, bgra, cv::COLOR_GRAY2BGRA);
    }
    else {
        g_fileWorker.badImageEntries.insert(std::move(badKey));
        if (outStatus) {
            *outStatus = L"图片通道数不受支持(已加入损坏列表):\n" + filePath;
        }
        return false;
    }
    outImage = bgra;
    return true;
}

void remove_old_preload_cache() {
	assert(mode == FileMode_Zip);
    //清理过远的缓存
    for (auto it = g_worker.preloadCache.begin(); it != g_worker.preloadCache.end(); ) {
        int idx = it->first;
        if (std::abs(idx - g_worker.imageIndex) > 2) {
            dbgprintf("Removing distant cached image at index %d\n", idx);
            it = g_worker.preloadCache.erase(it);
        }
        else {
            ++it;
        }
    }
}
void remove_old_preload_cache_file() {
    assert(mode == FileMode_Folder);
    //清理过远的缓存
    for (auto it = g_fileWorker.preloadCache.begin(); it != g_fileWorker.preloadCache.end(); ) {
        int idx = it->first;
        if (std::abs(idx - g_fileWorker.imageIndex) > 2) {
            dbgprintf("Removing distant cached image at index %d\n", idx);
            it = g_fileWorker.preloadCache.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool refresh_preload_neighbors() {
	assert(mode == FileMode_Zip);
    if (g_worker.zipIndex < 0 || g_worker.zipIndex >= (int)g_worker.zipFiles.size()) return false;
    if (g_worker.imageEntries.empty() || g_worker.imageIndex < 0 || g_worker.imageIndex >= (int)g_worker.imageEntries.size()) return false;

    const std::wstring& zipPath = g_worker.zipFiles[g_worker.zipIndex];
    const int targets[] = { g_worker.imageIndex + 2, g_worker.imageIndex + 1, g_worker.imageIndex - 1 , g_worker.imageIndex - 2 };
    bool notAllCached = false;
    for (int idx : targets) {
        if (idx < 0 || idx >= (int)g_worker.imageEntries.size()) continue;
        if (g_worker.preloadCache.find(idx) != g_worker.preloadCache.end()) {
            continue;
        }
        cv::Mat img;
        if (decode_image_from_zip_entry(zipPath, g_worker.imageEntries[idx], img, nullptr)) {
            g_worker.preloadCache[idx] = img;
			dbgprintf("Preloaded image at index %d\n", idx);
			notAllCached = true;
            break;
        }
    }
    if (notAllCached == false)
    {
		remove_old_preload_cache();
    }
    return notAllCached;
}
bool refresh_preload_neighbors_file() {
    assert(mode == FileMode_Folder);
    if (g_fileWorker.imageFiles.empty() || g_fileWorker.imageIndex < 0 || g_fileWorker.imageIndex >= (int)g_fileWorker.imageFiles.size()) return false;
    const int targets[] = { g_fileWorker.imageIndex + 2, g_fileWorker.imageIndex + 1, g_fileWorker.imageIndex - 1 , g_fileWorker.imageIndex - 2 };
    bool notAllCached = false;
    for (int idx : targets) {
        if (idx < 0 || idx >= (int)g_fileWorker.imageFiles.size()) continue;
        if (g_fileWorker.preloadCache.find(idx) != g_fileWorker.preloadCache.end()) {
            continue;
        }
        cv::Mat img;
        if (decode_imagefile(g_fileWorker.imageFiles[idx], img, nullptr)) {
            g_fileWorker.preloadCache[idx] = img;
            dbgprintf("Preloaded image at index %d\n", idx);
            notAllCached = true;
            break;
        }
    }
    if (notAllCached == false)
    {
        remove_old_preload_cache_file();
    }
    return notAllCached;
}

bool load_current_image() {
	assert(mode == FileMode_Zip);
    if (g_worker.zipIndex < 0 || g_worker.zipIndex >= (int)g_worker.zipFiles.size()) return false;
    if (g_worker.imageIndex < 0 || g_worker.imageIndex >= (int)g_worker.imageEntries.size()) return false;

    auto it = g_worker.preloadCache.find(g_worker.imageIndex);
    if (it != g_worker.preloadCache.end()) {
        g_worker.currentImage = it->second;
        g_worker.statusMessage.clear();
        return true;
    }

    const std::wstring& zipPath = g_worker.zipFiles[g_worker.zipIndex];
    const std::string& entryName = g_worker.imageEntries[g_worker.imageIndex];

    cv::Mat decoded;
    std::wstring status;
    if (!decode_image_from_zip_entry(zipPath, entryName, decoded, &status)) {
        g_worker.currentImage.release();
        g_worker.statusMessage = status;
        return false;
    }

    g_worker.currentImage = decoded;
    g_worker.statusMessage.clear();
    //当前图片加入缓存
	g_worker.preloadCache[g_worker.imageIndex] = decoded;
    return true;
}
bool load_current_image_file() {
    assert(mode == FileMode_Folder);
    if (g_fileWorker.imageIndex < 0 || g_fileWorker.imageIndex >= (int)g_fileWorker.imageFiles.size()) return false;
    const std::wstring& filePath = g_fileWorker.imageFiles[g_fileWorker.imageIndex];
    cv::Mat decoded;
    std::wstring status;
    if (!decode_imagefile(filePath, decoded, &status)) {
        g_fileWorker.currentImage.release();
        g_fileWorker.statusMessage = status;
        return false;
    }
    g_fileWorker.currentImage = decoded;
    g_fileWorker.statusMessage.clear();
    return true;
}

bool open_zip_at(int index) {
	assert(mode == FileMode_Zip);
	if (index < 0 || index >= (int)g_worker.zipFiles.size()) return false;

	const std::wstring& zipPath = g_worker.zipFiles[index];
	if (!open_archive_for_zip(zipPath)) return false;

	std::vector<std::string> entries;
	if (!enumerate_image_entries_in_archive(entries)) {
		return false;
	}

	if (entries.empty()) return false;

	std::sort(entries.begin(), entries.end(),
		[](const std::string& a, const std::string& b) { return natural_less_icase(a, b); });

	g_worker.zipIndex = index;
	g_worker.imageEntries = std::move(entries);
	g_worker.imageIndex = -1;
	g_worker.preloadCache.clear();

	for (int i = 0; i < (int)g_worker.imageEntries.size(); ++i) {
		g_worker.imageIndex = i;
		if (load_current_image()) {
			return true;
		}
	}

	g_worker.statusMessage = L"当前压缩包内图片均不可读取(已跳过):\n" + g_worker.zipFiles[g_worker.zipIndex];
	return false;
}

bool open_zip_by_step(int startIndex, int step) {
	assert(mode == FileMode_Zip);
    for (int i = startIndex; i >= 0 && i < (int)g_worker.zipFiles.size(); i += step) {
        if (open_zip_at(i)) {
            return true;
        }
    }
    return false;
}

bool show_next_image() {
	assert(mode == FileMode_Zip);
    if (g_worker.zipIndex < 0) return false;

    for (int i = g_worker.imageIndex + 1; i < (int)g_worker.imageEntries.size(); ++i) {
        g_worker.imageIndex = i;
        if (load_current_image()) {
            return true;
        }
    }
    return false;
}

bool show_prev_image() {
    assert(mode == FileMode_Zip);
    if (g_worker.zipIndex < 0) return false;

    for (int i = g_worker.imageIndex - 1; i >= 0; --i) {
        g_worker.imageIndex = i;
        if (load_current_image()) {
            return true;
        }
    }
    return false;
}
bool show_next_image_file() {
    assert(mode == FileMode_Folder);
    if (g_fileWorker.imageIndex < 0) return false;
    for (int i = g_fileWorker.imageIndex + 1; i < (int)g_fileWorker.imageFiles.size(); ++i) {
        g_fileWorker.imageIndex = i;
        if (load_current_image_file()) {
            return true;
        }
    }
    return false;
}
bool show_prev_image_file() {
    assert(mode == FileMode_Folder);
    if (g_fileWorker.imageIndex < 0) return false;
    for (int i = g_fileWorker.imageIndex - 1; i >= 0; --i) {
        g_fileWorker.imageIndex = i;
        if (load_current_image_file()) {
            return true;
        }
    }
    return false;
}

bool move_file_to_recycle_bin(const std::wstring& path) {
    std::wstring from = path;
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}
ULONGLONG lastDeleteTick = 0;
void delete_current_zip() {
    assert(mode == FileMode_Zip);
    if (g_worker.imageIndex < 3 && g_worker.imageIndex < ((int)g_worker.imageEntries.size() - 1))
    {
        if (GetTickCount64() - lastDeleteTick < 1000 * 3) {
            g_worker.statusMessage = L"请勿频繁删除文件!";
            return;
        }
    }
    if (g_worker.zipIndex < 0 || g_worker.zipIndex >= (int)g_worker.zipFiles.size()) return;

    int oldIndex = g_worker.zipIndex;
    close_open_archive();
    std::wstring deletingPath = g_worker.zipFiles[oldIndex];

    if (!move_file_to_recycle_bin(deletingPath)) return;
	lastDeleteTick = GetTickCount64();

    g_worker.zipFiles.erase(g_worker.zipFiles.begin() + oldIndex);

    if (!g_worker.zipFiles.empty()) {
        int nextIndex = oldIndex;
        if (nextIndex >= (int)g_worker.zipFiles.size()) nextIndex = (int)g_worker.zipFiles.size() - 1;

        if (!open_zip_at(nextIndex)) {
            if (!open_zip_by_step(nextIndex + 1, 1)) {
                open_zip_by_step(nextIndex - 1, -1);
            }
        }
        return;
    }

    g_worker.zipIndex = -1;
    g_worker.imageIndex = -1;
    g_worker.imageEntries.clear();
    g_worker.currentImage.release();
    g_worker.preloadCache.clear();

    if (!g_worker.currentFolder.empty()) {
        scan_zip_files_in_folder(g_worker.currentFolder);
        if (!g_worker.zipFiles.empty()) {
            open_zip_at(0);
            return;
        }
		else {
			g_worker.statusMessage = L"当前文件夹内无压缩文件:\n" + g_worker.currentFolder;
		}
	}
}
void delete_current_folder() {
    assert(mode == FileMode_Folder);
	if(g_fileWorker.currentFolder.empty()) return;
    std::wstring deletingPath = g_fileWorker.currentFolder;
    close_open_archive();
    if (!move_file_to_recycle_bin(deletingPath)) return;
    fs::path selectedPath(deletingPath);
    g_fileWorker.currentFolder = selectedPath.parent_path().wstring();
    g_fileWorker.currentImage.release();
    g_fileWorker.imageFiles.clear();
    g_fileWorker.imageIndex = -1;
    g_fileWorker.preloadCache.clear();
	g_fileWorker.statusMessage = L"当前文件夹已删除:\n" + deletingPath;
	dbgprintf(L"Deleted folder: %s\n", deletingPath.c_str());
}

void CheckPreloadCacheNeedClear();
void PreloadNeighbors();
std::shared_ptr<WorkerResult> OpenPath(const std::wstring& filePath) {
    if (is_supported_archive_file(filePath)) {
        return g_messageThread.send([filePath] -> std::shared_ptr<WorkerResult> {
            mode = FileMode_Zip;
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            fs::path selectedPath(filePath);
            std::wstring folder = selectedPath.parent_path().wstring();
            scan_zip_files_in_folder(folder);
            auto it = std::find(g_worker.zipFiles.begin(), g_worker.zipFiles.end(), selectedPath.wstring());
            if (it != g_worker.zipFiles.end()) {
                int selectedIndex = (int)std::distance(g_worker.zipFiles.begin(), it);
                result->success = open_zip_at(selectedIndex);
                if (!result->success) {
                    if (!open_zip_by_step(selectedIndex + 1, 1)) {
                        result->success = open_zip_by_step(selectedIndex - 1, -1);
                    }
                    else {
                        result->success = true;
                    }
                }
            }
            fill_snapshot(result->snapshot);
            PreloadNeighbors();
            return result;
            });
    }
    else {
        return g_messageThread.send([filePath] -> std::shared_ptr<WorkerResult> {
            mode = FileMode_Folder;
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            fs::path selectedPath(filePath);
            std::wstring folder = selectedPath.parent_path().wstring();
            scan_images_in_folder(folder);
            if (!g_fileWorker.imageFiles.empty()) {
                g_fileWorker.imageIndex = 0;
				auto it = std::find(g_fileWorker.imageFiles.begin(), g_fileWorker.imageFiles.end(), selectedPath.wstring());
                if (it != g_fileWorker.imageFiles.end()) {
                    g_fileWorker.imageIndex = (int)std::distance(g_fileWorker.imageFiles.begin(), it);
                }
                result->success = load_current_image_file();
            }
            else {
                result->success = false;
                g_fileWorker.statusMessage = L"当前文件夹内无图片文件:\n" + folder;
            }
            fill_snapshot(result->snapshot);
            PreloadNeighbors();
            return result;
			});
    }
}
std::shared_ptr<WorkerResult> ShowNextImage() {
    if (mode == FileMode_Zip) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            result->success = show_next_image();
            fill_snapshot(result->snapshot);
            CheckPreloadCacheNeedClear();
            PreloadNeighbors();
            return result;
            });
    }
    else if(mode== FileMode_Folder) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            result->success = show_next_image_file();
            fill_snapshot(result->snapshot);
            CheckPreloadCacheNeedClear();
            PreloadNeighbors();
            return result;
            });
    }
	return nullptr;
}
std::shared_ptr<WorkerResult> ShowPrevImage() {
    if (mode == FileMode_Zip) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            result->success = show_prev_image();
            fill_snapshot(result->snapshot);
            CheckPreloadCacheNeedClear();
            PreloadNeighbors();
            return result;
            });
	}
	else if (mode == FileMode_Folder) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            result->success = show_prev_image_file();
            fill_snapshot(result->snapshot);
            CheckPreloadCacheNeedClear();
            PreloadNeighbors();
            return result;
            });
    }
	return nullptr;
}
std::shared_ptr<WorkerResult> ShowNextZip() {
    if (mode == FileMode_Zip) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            if (!g_worker.zipFiles.empty()) {
                result->success = open_zip_by_step(g_worker.zipIndex + 1, 1);
            }
            fill_snapshot(result->snapshot);
            return result;
            });
    }
	return nullptr;
}
std::shared_ptr<WorkerResult> ShowPrevZip() {
    if (mode == FileMode_Zip) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            if (!g_worker.zipFiles.empty()) {
                result->success = open_zip_by_step(g_worker.zipIndex - 1, -1);
            }
            fill_snapshot(result->snapshot);
            return result;
            });
    }
	return nullptr;
}
std::shared_ptr<WorkerResult> DeleteCurrentFile() {
    if (mode == FileMode_Zip) {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            delete_current_zip();
            result->success = true;
            fill_snapshot(result->snapshot);
            return result;
            });
    }
    else if (mode == FileMode_Folder)
    {
        return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
            std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
            delete_current_folder();
            result->success = true;
            fill_snapshot(result->snapshot);
            return result;
            });
    }
	return nullptr;
}
std::shared_ptr<WorkerResult> GetSnapshot() {
    return g_messageThread.send([]() -> std::shared_ptr<WorkerResult> {
        std::shared_ptr<WorkerResult> result = std::make_shared<WorkerResult>();
        result->success = true;
        fill_snapshot(result->snapshot);
        return result;
        });
}
void PreloadNeighbors() {
    if (mode == FileMode_Zip) {
        g_messageThread.post_idle([]() {
            if (refresh_preload_neighbors()) {
                PreloadNeighbors();
            }
            });
    }
    else if (mode == FileMode_Folder)
    {
        g_messageThread.post_idle([]() {
            if (refresh_preload_neighbors_file()) {
                PreloadNeighbors();
            }
            });
    }
}
void CheckPreloadCacheNeedClear() {
    if(mode==FileMode_Zip)
    {
        if (g_worker.preloadCache.size() > 10) {
            g_messageThread.post([] {
                remove_old_preload_cache();
                });
        }
    }
    else if(mode == FileMode_Folder)
    {
        if (g_fileWorker.preloadCache.size() > 10) {
            g_messageThread.post([] {
                remove_old_preload_cache_file();
                });
        }
	}
}