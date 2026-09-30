#pragma once

struct BackProcessConfig
{
    std::wstring ali_key;
    std::wstring ocr_server_url;
    std::wstring voicevox_server_url;
    int voicevox_speaker_id = 20;
    float voicevox_speed_scale = 1.0f;
};

void ocr_image(const HWND backWnd, const cv::Mat image);
void play_sound(std::wstring text);
void replay_sound();
bool load_backprocess_config();
BackProcessConfig get_backprocess_config();
bool save_backprocess_config(const BackProcessConfig& config);
void start_translation(HWND hWnd, std::wstring text);
std::wstring check_translation_cache(std::wstring text);
void back_ocr_and_play(const cv::Mat image);
