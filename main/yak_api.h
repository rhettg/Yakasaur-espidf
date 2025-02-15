#ifndef YAK_API_H
#define YAK_API_H

#include "esp_err.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Structure to hold stream messages
typedef struct {
    char stream_name[32];
    char *data;
} yak_stream_message_t;

// Public API
esp_err_t yak_api_publish(const char *stream_name, const char *content_type, const char *data, size_t data_len);
esp_err_t yak_api_init(void);
QueueHandle_t yak_api_get_queue(void);
void yak_api_subscription_task(void *pvParameters);

#endif
