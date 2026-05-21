#include "framework.h"
#include "BackProcess.h"
#include "../ComicWatch/MessageThread.h"
#include <iostream>
#define OPENSSL_SUPPRESS_DEPRECATED
#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_ZLIB_SUPPORT
#include <httplib.h>
#include <zip.h>
#include <json.hpp>
#include <mmsystem.h>
#include <fkYAML/node.hpp>

class AliTransClient {
private:
    std::string api_key;
    httplib::Client cli{"https://dashscope.aliyuncs.com"};
    const std::string url = "/compatible-mode/v1";
public:
    AliTransClient() {}

    void set_api_key(const std::string& key) {
        api_key = key;
    }

    // 类似 Python 的 client.chat.completions.create
    std::string chat_completions_create(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& extra_body = nlohmann::json::object()
    ) {
        if (api_key.empty()) {
            throw std::runtime_error("API key not set");
        }

        cli.set_read_timeout(60, 0);   // 60秒超时
        cli.set_write_timeout(60, 0);

        nlohmann::json body = {
            {"model", model},
            {"messages", messages}
        };

        // 合并 extra_body（例如 translation_options）
        if (!extra_body.empty()) {
            for (auto& [key, value] : extra_body.items()) {
                body[key] = value;
            }
        }

        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key},
            {"Content-Type", "application/json"}
        };

        auto res = cli.Post(url + "/chat/completions", headers, body.dump(), "application/json");

        if (!res || res->status != 200) {
            std::string err = "HTTP error: ";
            if (res) err += std::to_string(res->status) + " " + res->body;
            else err += "No response";
            std::cout << err << std::endl;
            throw std::runtime_error(err);
        }

        nlohmann::json response = nlohmann::json::parse(res->body);

        // 提取 content
        if (response.contains("choices") &&
            response["choices"].is_array() &&
            !response["choices"].empty() &&
            response["choices"][0].contains("message") &&
            response["choices"][0]["message"].contains("content")) {

            return response["choices"][0]["message"]["content"].get<std::string>();
        }

        throw std::runtime_error("Invalid response format");
    }
};

MessageThread workthread;
namespace fs = std::filesystem;
std::wstring get_yaml_path() {
    WCHAR exePath[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"ComicWatch.yaml";
    fs::path iniPath(exePath);
    iniPath.replace_extension(L".yaml");
    return iniPath.wstring();
}
std::string wstring_to_utf8(const std::wstring& wstr)
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
bool SplitUrlToOriginAndPath(const std::string& url, std::string& origin, std::string& path) {
	size_t scheme_end = url.find("://");
	if (scheme_end == std::string::npos) {
		return false; // 无效的URL
	}
	size_t host_start = scheme_end + 3;
	size_t path_start = url.find('/', host_start);
	if (path_start == std::string::npos) {
        origin = url;
		path = "/";
	}
	else {
        origin = url.substr(0, path_start);
		path = url.substr(path_start);
	}
	return true;
}
std::string ali_key;
std::string ocr_origin;
std::string ocr_path;
std::string voicevox_server_url;
int voicevox_speaker_id=20;
float voicevox_speed_scale = 1.0f;

httplib::Client ocr_client("");
httplib::Client voicevox_client("");
AliTransClient ali_client;
bool load_backprocess_config() {
    try {
        auto yamlPath = get_yaml_path();

        fs::path path(yamlPath);
        std::ifstream file(path, std::ios::binary);

        // 2. 检查文件是否成功打开
        if (!file.is_open()) {
            dbgprintf(L"无法打开配置文件！路径: %s\n", yamlPath.c_str());
            return false;
        }

        auto node = fkyaml::node::deserialize(file);
        file.close();

        auto& keyNode = node["key"];
        ali_key = keyNode["ali_key"].get_value<std::string>();
        auto& ocrNode = node["ocr"];
        auto ocr_server_url = ocrNode["server_url"].get_value<std::string>();
        SplitUrlToOriginAndPath(ocr_server_url, ocr_origin, ocr_path);
        auto& voicevoxNode = node["voicevox"];
        voicevox_server_url = voicevoxNode["src"].get_value<std::string>();
        auto& spkid = voicevoxNode["speaker_id"];
		if (spkid.is_string()) {
			voicevox_speaker_id = std::stoi(spkid.get_value<std::string>());
		}
		else if (spkid.is_integer()) {
			voicevox_speaker_id = spkid.get_value<int>();
		}
		auto& spdscale = voicevoxNode["speed_scale"];
        if (spdscale.is_string()) {
            voicevox_speed_scale = std::stof(spdscale.get_value<std::string>());
		}
		else if (spdscale.is_float_number()) {
			voicevox_speed_scale = spdscale.get_value<float>();
		}

        ocr_client = httplib::Client(ocr_origin);
        voicevox_client = httplib::Client(voicevox_server_url);
        ali_client.set_api_key(ali_key);
        return true;
	}
    catch (const std::exception& ex) {
		std::string msg = std::string("config load error: ") + ex.what();
        MessageBoxA(nullptr, msg.c_str(), "error", MB_OK | MB_ICONINFORMATION);
        return false;
    }
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
std::string url_encode(const std::string& s)
{
    std::string result;
    result.reserve(s.size() * 3);  // 预分配空间

    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += c;
        }
        else
        {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            result.append(hex);
        }
    }
    return result;
}
void ocr_image(const HWND backWnd,const cv::Mat image) {
	workthread.post([backWnd, image] {
        std::vector<uchar> webp_buffer;
        std::vector<int> params = {
            cv::IMWRITE_WEBP_QUALITY, 80   // 1~100，越大质量越好，文件越大
            // cv::IMWRITE_WEBP_LOSSLESS_MODE, 0  // 可选：无损模式（0=有损，1=无损）
        };
        bool success = cv::imencode(".webp", image, webp_buffer, params);
        if (success) {
            printf("Image encoded to WebP successfully. Buffer size: %zu bytes\n", webp_buffer.size());
        }
        else {
            printf("Failed to encode image to WebP format.\n");
        }

        auto res = ocr_client.Post(ocr_path, (char*)webp_buffer.data(), webp_buffer.size(), "image/webp");
        if (res) {
			dbgprintf("OCR request completed with status: %d body %s\n", res->status, res->body.c_str());
            auto json = nlohmann::json::parse(res->body);
            auto msg = std::make_unique<std::wstring>(utf8_to_wstring(json["result"].get<std::string>()));
            if (PostMessage(backWnd, WM_USER_OCRFINISH, 0, reinterpret_cast<LPARAM>(msg.get()))) {
				msg.release();
            }
        }
        });
}
std::string voicevox_sound_buffer;
void play_sound(std::wstring text) {
	workthread.post([text = std::move(text)] {
        std::string path = "/audio_query?text=" + url_encode(wstring_to_utf8(text)) +
            "&speaker=" + std::to_string(voicevox_speaker_id);
        auto res = voicevox_client.Post(path);
        if (res) {
            //res->status;
            //res->body;
            if (res->status == 200) {
                auto json = nlohmann::json::parse(res->body);
                //dbgprintf("Status: %d bodylen: %zu\n", res->status, res->body.size());
                //dbgprintf("Body: %s\n", res->body.c_str());
                // 固定语速，避免引擎侧预设导致播放听感异常偏快。
                json["speedScale"] = voicevox_speed_scale;
                // 显式提升输出采样率，降低播放器按 48k 假设解码时产生“倍速感”的风险。
                auto sendbody=json.dump();

                std::string path2 = "/synthesis?speaker=" + std::to_string(voicevox_speaker_id);
                auto res2 = voicevox_client.Post(path2, sendbody.c_str(), sendbody.size(), "application/json");
                if (res2) {
                    dbgprintf("Status: %d bodylen: %zu\n", res2->status, res2->body.size());
                    //dbgprintf("Body: %s\n", res2->body.c_str());
					voicevox_sound_buffer = std::move(res2->body);
                    PlaySoundA(voicevox_sound_buffer.c_str(), NULL, SND_MEMORY | SND_ASYNC);
                }
            }
        }
        });
}
void replay_sound() {
	workthread.post([] {
		if (!voicevox_sound_buffer.empty()) {
			PlaySoundA(voicevox_sound_buffer.c_str(), NULL, SND_MEMORY | SND_ASYNC);
		}
		});
}

// 翻译函数
std::string translate_with_ali(
    const std::string& text = "Hello, world!",
    const std::string& source_lang = "Japanese",
    const std::string& target_lang = "Chinese"
) {
    nlohmann::json messages = nlohmann::json::array({
        {{"role", "user"}, {"content", text}}
        });

    nlohmann::json translation_options = {
        {"source_lang", source_lang},
        {"target_lang", target_lang}
    };

    nlohmann::json extra_body = {
        {"translation_options", translation_options}
    };

    std::string translated_text = ali_client.chat_completions_create(
        "qwen-mt-flash",      // 或你想用的模型
        messages,
        extra_body
    );
    return translated_text;
}

template<typename T>
class RecentCache {
private:
    std::deque<std::pair<std::basic_string<T>, std::basic_string<T>>> items;
    const size_t max_size = 3;

public:
    void put(std::basic_string<T> key, std::basic_string<T> value) {
        auto it = std::find_if(items.begin(), items.end(),
            [&](const auto& p) { return p.first == key; });

        if (it != items.end()) {
            items.erase(it);
        }

        items.emplace_front(std::move(key), std::move(value));

        if (items.size() > max_size) {
            items.pop_back();
        }
    }

    std::optional<std::basic_string<T>> get(const std::basic_string<T>& key) {
        auto it = std::find_if(items.begin(), items.end(),
            [&](const auto& p) { return p.first == key; });

        if (it == items.end()) {
            return std::nullopt;
        }

		return it->second;
		//访问后移到最前面
        auto pair = std::move(*it);
        items.erase(it);
        items.emplace_front(std::move(pair));

        return pair.second;
    }

    size_t size() const { return items.size(); }

    const std::deque<std::pair<std::basic_string<T>, std::basic_string<T>>>& getAll() const {
        return items;
    }
};

RecentCache<wchar_t> translation_cache;
void start_translation(HWND hWnd,std::wstring text) {
	workthread.post([hWnd, text = std::move(text)] {
		std::string utf8_text = wstring_to_utf8(text);
		std::string translated = translate_with_ali(utf8_text);
		std::wstring w_translated = utf8_to_wstring(translated);
		translation_cache.put(text, w_translated);
		auto msg = std::make_unique<std::wstring>(std::move(w_translated));
		if (PostMessage(hWnd, WM_USER_TRANSFINISH, 0, reinterpret_cast<LPARAM>(msg.get()))) {
			msg.release();
		}
		});
}
std::wstring check_translation_cache(std::wstring text) {
	return workthread.send([text = std::move(text)] -> std::wstring {
		auto w_translated = translation_cache.get(text);
		return w_translated ? *w_translated : std::wstring();
		});
}