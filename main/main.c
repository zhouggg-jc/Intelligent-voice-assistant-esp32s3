#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "http.h"
#include "inmp441.h"
#include "max98357.h"
#include "nvs_flash.h"
#include "wifi.h"
#include <stdio.h>

const char *TAG = "main";
int16_t *audio_buf = NULL;
size_t audio_buf_size;

TaskHandle_t asr_http_task_handle;
QueueHandle_t asr_data_queue;
QueueHandle_t llm_data_queue;

extern i2s_chan_handle_t rx_handle;
extern i2s_chan_handle_t tx_handle;

void device_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    inmp441_init();
    max98357_init();

    wifi_init_sta("zhouggg", "zjc021104", 5);
}

uint8_t key_get_level(gpio_num_t gpio_num)
{
    if (gpio_get_level(gpio_num) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (gpio_get_level(gpio_num) == 0)
            return 0;
    } else {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (gpio_get_level(gpio_num) == 1)
            return 1;
    }
    return 1;
}

void key_recording_task()
{
    uint8_t flag = 0;
    int32_t read_raw_buf[1024];
    size_t audio_buf_offset = 0;
    size_t bytes_read = 0;
    while (1) {
        if (key_get_level(GPIO_NUM_0) == 0 && flag == 0) {
            ESP_LOGI(TAG, "Start recording");
            flag = 1;
            free(audio_buf);
            audio_buf = NULL;
            audio_buf_size = 0;
            audio_buf_offset = 0;
        }
        if (key_get_level(GPIO_NUM_0) == 0 && flag == 1) {
            if (i2s_channel_read(rx_handle, read_raw_buf, 1024 * sizeof(int32_t), &bytes_read, 1000) == ESP_OK) {
                size_t new_samples = bytes_read / sizeof(int32_t);

                audio_buf_size += new_samples * sizeof(int16_t);
                audio_buf = realloc(audio_buf, audio_buf_size);

                if (audio_buf == NULL) {
                    ESP_LOGE(TAG, "realloc failed");
                    break;
                }

                for (size_t i = 0; i < new_samples; i++) {
                    audio_buf[audio_buf_offset / sizeof(int16_t) + i] = (int16_t)(read_raw_buf[i] >> 15);
                }

                audio_buf_offset += new_samples * sizeof(int16_t);
            }
        }
        if (key_get_level(GPIO_NUM_0) == 1 && flag == 1) {
            ESP_LOGI(TAG, "Recording done");
            flag = 0;
            xTaskNotifyGive(asr_http_task_handle);
        }
    }
}

void asr_http_task()
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        asr_proc(audio_buf, audio_buf_size);
    }
}

void llm_http_task()
{
    char *asr_result_data = NULL;
    while (1) {
        if (xQueueReceive(asr_data_queue, &asr_result_data, portMAX_DELAY) == pdTRUE) {
            llm_proc(asr_result_data);
            free(asr_result_data);
            asr_result_data = NULL;
        }
    }
}

void tts_http_task()
{
    char *llm_result_data = NULL;
    while (1) {
        if (xQueueReceive(llm_data_queue, &llm_result_data, portMAX_DELAY) == pdTRUE) {
            tts_proc(llm_result_data);
            free(llm_result_data);
            llm_result_data = NULL;
        }
    }
}

void app_main(void)
{
    device_init();
    asr_data_queue = xQueueCreate(5, sizeof(char *));
    llm_data_queue = xQueueCreate(5, sizeof(char *));

    xTaskCreate(key_recording_task, "key_recording_task", 8192, NULL, 5, NULL);
    xTaskCreate(asr_http_task, "asr_http_task", 4096, NULL, 5, &asr_http_task_handle);
    xTaskCreate(llm_http_task, "llm_http_task", 4096, NULL, 5, NULL);
    xTaskCreate(tts_http_task, "tts_http_task", 4096, NULL, 5, NULL);
}
