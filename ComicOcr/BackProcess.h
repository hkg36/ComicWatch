#pragma once
void ocr_image(const HWND backWnd, const cv::Mat image);
void play_sound(std::wstring text);
void replay_sound();
bool load_backprocess_config();
void start_translation(HWND hWnd, std::wstring text);
std::wstring check_translation_cache(std::wstring text);