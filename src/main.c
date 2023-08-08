#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "local.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "DEMO";

static EventGroupHandle_t wifi_event_group;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void initialise_wifi(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(nvs_flash_init()); // Initialize NVS
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "start the WIFI SSID:[%s]", WIFI_SSID);
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

void send_telemetry(void)
{
    char api_url[100];
    cJSON *root;
    cJSON *body;

    ESP_LOGI(TAG, "Collecting telemetry");
	root = cJSON_CreateObject();
    body = cJSON_CreateObject();

    uint32_t seconds_since_boot = esp_timer_get_time() / 1000000;

    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    ESP_ERROR_CHECK(ret);

    body = cJSON_AddObjectToObject(root, "body");
    cJSON_AddNumberToObject(body, "seconds_since_boot", seconds_since_boot);
    cJSON_AddNumberToObject(body, "wifi_rssi", ap_info.rssi);

    char *post_data = cJSON_Print(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sending telemetry");

    if (0 >= snprintf(api_url, 100, "%s/v1/missions/%s/notes", YAK_GDS_URL, YAK_GDS_MISSION)) {
        ESP_LOGE(TAG, "Failed to create API URL");
        return;
    }

    esp_http_client_config_t config = {
        .url = api_url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Content-Type", "application/json"));

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST request was successful");
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(post_data);
}

#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 1024
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    // Cribbed from esp-idf example code
    // https://github.com/espressif/esp-idf/blob/53ff7d43dbff642d831a937b066ea0735a6aca24/examples/protocols/esp_http_client/main/esp_http_client_example.c
    //
    // Apparently the expected way to get access to the HTTP body is to pull it
    // from events. For now that's all we're using it for.
    //
    // Modifications were made that allows for chunked encoding (because a lot
    // of servers like Rails support it by default) Also there is no support for
    // not providing a user_data buffer. What a strange API ESP-IDF has.

    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
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
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }

            int copy_len = 0;
            if (evt->user_data) {
                // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len) {
                    memcpy(evt->user_data + output_len, evt->data, copy_len);
                }
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

void get_commands() {
    char response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    char api_url[100];

    if (0 >= snprintf(api_url, 100, "%s/v1/missions/%s/note_queue", YAK_GDS_URL, YAK_GDS_MISSION)) {
        ESP_LOGE(TAG, "Failed to create API URL for commands");
        return;
    }

    esp_http_client_config_t config = {
        .url = api_url,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler,
        .user_data = response_buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "Retrieving commands");
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request for commands failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    ESP_LOGI(TAG, "HTTP GET request for commands succeeded");

    int content_length = esp_http_client_get_content_length(client);

    int buffer_content_length = strnlen(response_buffer, sizeof(response_buffer));
    ESP_LOGI(TAG, "Content length: %d, Response: %d", content_length, buffer_content_length);

    // Parse JSON response
    cJSON* root = cJSON_Parse(response_buffer);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return;
    }

    // Log any commands found
    cJSON* commands = cJSON_GetObjectItem(root, "commands.qi");
    if (cJSON_IsArray(commands)) {
        int num_commands = cJSON_GetArraySize(commands);
        ESP_LOGI(TAG, "Found %d commands", num_commands);
        for (int i = 0; i < num_commands; i++) {
        cJSON* note = cJSON_GetArrayItem(commands, i);
        cJSON* body = cJSON_GetObjectItem(note, "body");
        char* command = cJSON_GetObjectItem(body, "command")->valuestring;
        ESP_LOGI(TAG, "Received command: %s", command);
        }
    }

    cJSON_Delete(root);

  esp_http_client_cleanup(client);
}

void app_main()
{
    initialise_wifi();
    while(1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        send_telemetry();
        get_commands();
    }
}
