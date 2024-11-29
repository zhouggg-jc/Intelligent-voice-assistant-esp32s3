#include "esp_err.h"

esp_err_t asr_proc(const void *audio_data, const int audio_len);
esp_err_t llm_proc(const char *text);
esp_err_t tts_proc(const char *text);
