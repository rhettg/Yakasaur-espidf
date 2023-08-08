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

void app_main()
{
    initialise_wifi();
    while(1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        send_telemetry();
    }
}
