#include <stdio.h>
#include <math.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "local.h"

#define WIFI_CONNECTED_BIT BIT0

// Camera pins taken from CAMERA_MODEL_ESP32S3_EYE (https://github.com/Freenove/Freenove_Ultimate_Starter_Kit_for_ESP32/blob/7ada879b6dd7bae30ed03800d9c8ca2693590aaa/C/Sketches/Sketch_34.1_CameraWebServer/camera_pins.h#L251-L269)
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y2_GPIO_NUM 11
#define Y3_GPIO_NUM 9
#define Y4_GPIO_NUM 8
#define Y5_GPIO_NUM 10
#define Y6_GPIO_NUM 12
#define Y7_GPIO_NUM 18
#define Y8_GPIO_NUM 17
#define Y9_GPIO_NUM 16

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

static const char *TAG = "DEMO";

static EventGroupHandle_t wifi_event_group;

struct telemetryValues {
    double heading;
    double latitude;
    double longitude;
};

struct telemetryValues telemetry_values;

void cmd_fwd(int arg) {
    ESP_LOGI(TAG, "FWD %d", arg);

    double d = arg / 1000000.0;  // Convert to a smaller value for demo purposes
    double theta = telemetry_values.heading * M_PI / 180.0;  // Convert to radians

    telemetry_values.latitude += d * cos(theta);
    telemetry_values.longitude += d * sin(theta) / cos(telemetry_values.latitude * M_PI / 180.0);
}

void cmd_bck(int arg) {
    ESP_LOGI(TAG, "BCK %d", arg);

    double d = -arg / 1000000.0;  // Convert to a smaller value and make it negative for backward
    double theta = telemetry_values.heading * M_PI / 180.0;  // Convert to radians

    telemetry_values.latitude += d * cos(theta);
    telemetry_values.longitude += d * sin(theta) / cos(telemetry_values.latitude * M_PI / 180.0);
}

void cmd_rt(double angle) {
    ESP_LOGI(TAG, "RT %.2f", angle);
    telemetry_values.heading += angle;
    while (telemetry_values.heading >= 360.0) {
        telemetry_values.heading -= 360.0;
    }
}

void cmd_lt(double angle) {
    ESP_LOGI(TAG, "LT %.2f", angle);
    telemetry_values.heading -= angle;
    while (telemetry_values.heading < 0.0) {
        telemetry_values.heading += 360.0;
    }
}

void cmd_ping() {
    ESP_LOGI(TAG, "PING");
}

esp_err_t send_image(const char *filename, char *base64_data) {
    char api_url[100];
    cJSON *root;
    cJSON *body;

	root = cJSON_CreateObject();
    if (NULL == root) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return ESP_FAIL;
    }

    body = cJSON_AddObjectToObject(root, "body");
    if (NULL == body) {
        ESP_LOGE(TAG, "Failed to create JSON body");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (NULL == cJSON_AddStringToObject(body, "filename", filename)) {
        ESP_LOGE(TAG, "Failed to add filename to JSON");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (NULL == cJSON_AddStringToObject(body, "data", base64_data)) {
        ESP_LOGE(TAG, "Failed to add data to JSON");
        return ESP_FAIL;
    }

    char *post_data = cJSON_Print(root);
    if (NULL == post_data) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    if (0 >= snprintf(api_url, 100, "%s/v1/missions/%s/notes/images.qo", YAK_GDS_URL, YAK_GDS_MISSION)) {
        ESP_LOGE(TAG, "Failed to create API URL");
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = api_url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Content-Type", "application/json"));

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "Sending image to %s. Size %d", api_url, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST request was successful");
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        goto cleanupfail;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 201) {
        ESP_LOGE(TAG, "HTTP POST request returned status code %d", status_code);
        goto cleanupfail;
    }

    esp_http_client_cleanup(client);
    free(post_data);
    return ESP_OK;

cleanupfail:
    esp_http_client_cleanup(client);
    free(post_data);
    return ESP_FAIL;
}


void cmd_snap() {
    ESP_LOGI(TAG, "SNAP");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "failed to grab framebuffs");
        return;
    }

    // use fb->buf to access the image
    ESP_LOGI(TAG, "Picture taken! Its size was: %zu bytes", fb->len);

    // base64 encodes 3 bytes as 4. Add some ones for rounding and a null terminator.
    size_t encode_len = 1 + ((fb->len / 3) + 1) * 4;
    char* base64_buffer = malloc(encode_len);
    if (NULL == base64_buffer) {
        ESP_LOGE(TAG, "Failed to allocate base64 buffer");
        esp_camera_fb_return(fb);
        return;
    }

    size_t encoded_len = 0;
    int ret = mbedtls_base64_encode((unsigned char *)base64_buffer, encode_len, &encoded_len, fb->buf, fb->len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to encode image: %d (%d olen)", ret, encoded_len);
        free(base64_buffer);
        esp_camera_fb_return(fb);
        return;
    }

    ESP_LOGI(TAG, "Encoded image size: %zu bytes", encoded_len);

    esp_camera_fb_return(fb);

    esp_err_t err = send_image("camera1.jpg", base64_buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send image: %s", esp_err_to_name(err));
    }

    free(base64_buffer);
}

void handle_command(char *command) {
    char cmd_name[50];
    int cmd_arg;
    int num_args;

    ESP_LOGI(TAG, "Received command: %s", command);

    // Parse the command
    num_args = sscanf(command, "%s %d", cmd_name, &cmd_arg);

    // convert cmd_name to uppercase
    for (int i = 0; cmd_name[i]; i++) {
        cmd_name[i] = toupper(cmd_name[i]);
    }

    // Switch based on command name
    if (strcmp(cmd_name, "FWD") == 0) {
        if (num_args == 2) {
            cmd_fwd(cmd_arg);
        } else {
            cmd_fwd(0); // default argument value
        }
    } else if (strcmp(cmd_name, "BCK") == 0) {
        if (num_args == 2) {
            cmd_bck(cmd_arg);
        } else {
            cmd_bck(0); // default argument value
        }
    } else if (strcmp(cmd_name, "RT") == 0) {
        if (num_args == 2) {
            cmd_rt((double)cmd_arg);
        } else {
            ESP_LOGE(TAG, "Angle required for RT command");
        }
    } else if (strcmp(cmd_name, "LT") == 0) {
        if (num_args == 2) {
            cmd_lt((double)cmd_arg);
        } else {
            ESP_LOGE(TAG, "Angle required for LT command");
        }
    } else if (strcmp(cmd_name, "PING") == 0) {
        cmd_ping();
    } else if (strcmp(cmd_name, "SNAP") == 0) {
        cmd_snap();
    } else {
        ESP_LOGE(TAG, "Unknown command %s", cmd_name);
    }
}

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

    uint32_t seconds_since_boot = esp_timer_get_time() / 1000000;

    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    ESP_ERROR_CHECK(ret);

    body = cJSON_AddObjectToObject(root, "body");
    cJSON_AddNumberToObject(body, "seconds_since_boot", seconds_since_boot);
    cJSON_AddNumberToObject(body, "wifi_rssi", ap_info.rssi);

    cJSON_AddNumberToObject(body, "heading", telemetry_values.heading);

    cJSON *location;
    location = cJSON_AddObjectToObject(body, "location");
    cJSON_AddNumberToObject(location, "latitude", telemetry_values.latitude);
    cJSON_AddNumberToObject(location, "longitude", telemetry_values.longitude);

    char *post_data = cJSON_Print(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sending telemetry");

    if (0 >= snprintf(api_url, 100, "%s/v1/missions/%s/notes/telemetry.qo", YAK_GDS_URL, YAK_GDS_MISSION)) {
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
        goto cleanup;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 201) {
        ESP_LOGE(TAG, "HTTP POST request returned status code %d", status_code);
        goto cleanup;
    }

cleanup:
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
        handle_command(command);
        }
    }

    cJSON_Delete(root);

  esp_http_client_cleanup(client);
}

static camera_config_t camera_config = {
    .pin_pwdn  = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM,
    .pin_sccb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,//EXPERIMENTAL: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,//YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_UXGA,//QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1, //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM, //Choose the location of frame buffer
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY//CAMERA_GRAB_LATEST. Sets when buffers should be filled
};

void camera_init(){
    //power up the camera if PWDN pin is defined
    if(PWDN_GPIO_NUM != -1){
        ESP_ERROR_CHECK(gpio_set_direction(PWDN_GPIO_NUM, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(PWDN_GPIO_NUM, 0));
    }

    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return;
    }

    return;
}

void app_main()
{
    telemetry_values.heading = 0;
    telemetry_values.latitude = 47.816944;
    telemetry_values.longitude = -119.656111;

    initialise_wifi();
    camera_init();
    while(1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        send_telemetry();
        get_commands();
    }
}
