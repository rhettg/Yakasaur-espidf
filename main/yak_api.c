#include "yak_api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "local.h"

static const char *TAG = "YAK_API";


esp_err_t yak_api_publish(const char *stream_name, const char *content_type, const char *data, size_t data_len) {
    char url[256];
    esp_err_t ret = ESP_OK;

    // Construct URL
    snprintf(url, sizeof(url), "%s/v1/stream/%s", YAK_API_BASE_URL, stream_name);

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Content-Type", content_type));
    esp_http_client_set_post_field(client, data, data_len);

    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        ret = err;
    }

    // Cleanup
    esp_http_client_cleanup(client);

    return ret;
}


static QueueHandle_t stream_queue;
#define STREAM_QUEUE_SIZE 10
static esp_err_t stream_event_handler(esp_http_client_event_t *evt);
typedef struct {
    char stream_name[32];
    char buffer[1024];
    size_t buffer_len;
} stream_context_t;


void yak_api_subscription_task(void *pvParameters) {
    char *stream_name = (char *)pvParameters;
    char url[256];
    
    snprintf(url, sizeof(url), "%s/v1/stream/%s", YAK_API_BASE_URL, stream_name);
    
    while (1) {
        stream_context_t *context = malloc(sizeof(stream_context_t));
        context->buffer_len = 0;
        strncpy(context->stream_name, stream_name, sizeof(context->stream_name)-1);

        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 60000,
            .event_handler = stream_event_handler,
            .user_data = context,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL) {
            ESP_LOGI(TAG, "Starting subscription to %s", stream_name);
            
            while (1) {
                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK) {
                    break;
                }

                if (err == ESP_ERR_HTTP_EAGAIN) {
                    continue;
                }

                ESP_LOGE(TAG, "Stream connection failed: %s", esp_err_to_name(err));
                break;
            }
                
            esp_http_client_cleanup(client);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static esp_err_t stream_event_handler(esp_http_client_event_t *evt) {
    stream_context_t *context = (stream_context_t *)evt->user_data;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "Stream connected");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "Stream data");

            memcpy(context->buffer + context->buffer_len, evt->data, evt->data_len);
            context->buffer_len += evt->data_len;
            
            // Do we have a complete message, or should we wait until next time?
            if (evt->data_len > 0 && ((char *)evt->data)[evt->data_len-1] == '\n') {
                // Drop the newline
                context->buffer[context->buffer_len-1] = '\0';
                
                yak_stream_message_t msg;
                strncpy(msg.stream_name, context->stream_name, sizeof(msg.stream_name)-1);
                msg.data = strdup(context->buffer);
                
                if (msg.data) {
                    xQueueSend(stream_queue, &msg, 0);
                }
                
                context->buffer_len = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Stream disconnected");
            context->buffer_len = 0;
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t yak_api_init(void) {
    stream_queue = xQueueCreate(STREAM_QUEUE_SIZE, sizeof(yak_stream_message_t));
    if (stream_queue == NULL) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

QueueHandle_t yak_api_get_queue(void) {
    return stream_queue;
}
