#include "yak_api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "local.h"

static const char *TAG = "YAK_API";

// Simple ULID-like ID generator (not full ULID spec compliant)
static void generate_id(char *id_buf) {
    uint32_t timestamp = esp_random();
    uint32_t random = esp_random();
    sprintf(id_buf, "%08lx%08lx", timestamp, random);
}

esp_err_t yak_api_publish(const char *stream_name, cJSON *event) {
    char url[256];
    char id[17];
    esp_err_t ret = ESP_OK;

    // Add ID if not present
    if (!cJSON_HasObjectItem(event, "id")) {
        generate_id(id);
        cJSON_AddStringToObject(event, "id", id);
    }

    // Convert event to string
    char *post_data = cJSON_Print(event);
    if (!post_data) {
        return ESP_FAIL;
    }

    // Construct URL
    snprintf(url, sizeof(url), "%s/v1/stream/%s", YAK_API_BASE_URL, stream_name);

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(post_data);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Content-Type", "application/json"));
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        ret = err;
    }

    // Cleanup
    esp_http_client_cleanup(client);
    free(post_data);

    return ret;
}