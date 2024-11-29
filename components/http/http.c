#include "http.h"
#include "cJSON.h"
#include "driver/i2s_std.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include <stdio.h>
#include <sys/param.h>

static const char *TAG = "http";

esp_http_client_handle_t asr_client;
esp_http_client_handle_t llm_client;
esp_http_client_handle_t tts_client;

extern QueueHandle_t asr_data_queue;
extern QueueHandle_t llm_data_queue;
extern i2s_chan_handle_t tx_handle;

char *parse_asr_json(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Error parsing json");
        return NULL;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        ESP_LOGE(TAG, "result is not array");
        return NULL;
    }

    cJSON *item = cJSON_GetArrayItem(result, 0);
    if (!cJSON_IsString(item)) {
        ESP_LOGE(TAG, "item is not string");
        return NULL;
    }

    char *result_data = strdup(item->valuestring);
    printf("语音识别结果:%s\n", result_data);

    cJSON_Delete(root);
    return result_data;
}

char *parse_llm_json(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Error parsing json");
        return NULL;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsString(result)) {
        ESP_LOGE(TAG, "result is not string");
        return NULL;
    }

    char *result_data = strdup(result->valuestring);
    printf("大模型回复:%s\n", result_data);

    cJSON_Delete(root);
    return result_data;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (evt->client == tts_client) {
            i2s_channel_write(tx_handle, evt->data, evt->data_len, NULL, 1000);
        } else {
            int copy_len = 0;
            int content_len = esp_http_client_get_content_length(evt->client);
            if (output_buffer == NULL) {
                output_buffer = (char *)calloc(content_len + 1, sizeof(char));
                output_len = 0;
                if (output_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                    return ESP_FAIL;
                }
            }
            copy_len = MIN(evt->data_len, (content_len - output_len));
            if (copy_len) {
                memcpy(output_buffer + output_len, evt->data, copy_len);
            }
            output_len += copy_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL) {
            if (evt->client == asr_client) {
                char *result = parse_asr_json(output_buffer);
                xQueueSend(asr_data_queue, &result, 1000);
            } else if (evt->client == llm_client) {
                char *result = parse_llm_json(output_buffer);
                xQueueSend(llm_data_queue, &result, 1000);
            }
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0) {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL) {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

esp_err_t asr_proc(const void *audio_data, const int audio_len)
{
    esp_http_client_config_t http_client_cfg = {
        .method = HTTP_METHOD_POST,
        .url = "http://vop.baidu.com/server_api?dev_pid=1537&cuid=esp32s3&token=24.df0eca64673da4c8aaac5ece37809bd5.2592000.1734331487.282335-116177140",
        .event_handler = _http_event_handler,
    };
    asr_client = esp_http_client_init(&http_client_cfg);

    esp_http_client_set_header(asr_client, "Content-Type", "audio/pcm;rate=16000");
    esp_http_client_set_post_field(asr_client, audio_data, audio_len);

    esp_err_t err = esp_http_client_perform(asr_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(asr_client),
                 esp_http_client_get_content_length(asr_client));
    } else {
        ESP_LOGE(TAG, "HTTPS request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(asr_client);
    return err;
}

esp_err_t llm_proc(const char *text)
{
    esp_http_client_config_t http_client_cfg = {
        .method = HTTP_METHOD_POST,
        .url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/ernie_speed?access_token=24.b6d938b5c7e1a04620adf46e6ada0727.2592000.1735368515.282335-116433798",
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    llm_client = esp_http_client_init(&http_client_cfg);

    char post_data[512] = {0};
    sprintf(post_data, "{\"messages\": [{\"role\": \"user\", \"content\": \"%s(回答需简洁,不超过60个字)\"}]}", text);
    esp_http_client_set_post_field(llm_client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(llm_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(llm_client),
                 esp_http_client_get_content_length(llm_client));
    } else {
        ESP_LOGE(TAG, "HTTPS request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(llm_client);
    return err;
}

esp_err_t tts_proc(const char *text)
{
    esp_http_client_config_t http_client_cfg = {
        .method = HTTP_METHOD_POST,
        .url = "http://tsn.baidu.com/text2audio",
        .event_handler = _http_event_handler,
    };
    tts_client = esp_http_client_init(&http_client_cfg);

    char post_data[1024] = {0};
    sprintf(post_data, "tex=%s&ctp=1&cuid=esp32s3&lan=zh&spd=8&pit=5&vol=3&per=0&aue=4&tok=24.f88bc1d9932705c668f8430268e38d87.2592000.1735111577.282335-116300862", text);
    esp_http_client_set_post_field(tts_client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(tts_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(tts_client),
                 esp_http_client_get_content_length(tts_client));
    } else {
        ESP_LOGE(TAG, "HTTPS request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(tts_client);
    return err;
}
