#include <stdio.h>
#include <math.h>
#include <ctype.h>
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
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "driver/gpio.h"
#include "local.h"
#include "motor.h"
#include "yak_api.h"

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

#define ADC_CHANNEL     ADC_CHANNEL_3
#define ADC_WIDTH       ADC_BITWIDTH_12
#define ADC_ATTEN       ADC_ATTEN_DB_2_5
#define SAMPLES         16

static const char *TAG = "YAK";

static EventGroupHandle_t wifi_event_group;

struct telemetryValues {
    double heading;
    double latitude;
    double longitude;
};

struct telemetryValues telemetry_values;

extern const char isrg_root_pem_start[] asm("_binary_isrg_root_pem_start");
extern const char isrg_root_pem_end[]   asm("_binary_isrg_root_pem_end");

static adc_cali_handle_t adc_cali_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

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
    // Return this so we can get a fresh frame
    esp_camera_fb_return(fb);

    fb = esp_camera_fb_get();
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

    esp_err_t err = send_image("camera_front", base64_buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send image: %s", esp_err_to_name(err));
    }

    free(base64_buffer);
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

void init_adc() {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_WIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &channel_config));

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_2,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_WIDTH,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
}

float read_voltage() {
    int32_t adc_reading = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));
        adc_reading += raw;
    }
    adc_reading /= SAMPLES;

    int voltage;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_reading, &voltage));
    float scaled_voltage = (voltage * 6.0) / 0.5;
    scaled_voltage /= 1.08;

    return scaled_voltage;
}

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

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
    .frame_size = FRAMESIZE_QVGA,//QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 20, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1, //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM, //Choose the location of frame buffer
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
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

void send_telemetry_stream(void) {
    cJSON *root = cJSON_CreateObject();
    
    // Add telemetry data
    uint32_t seconds_since_boot = esp_timer_get_time() / 1000000;
    cJSON_AddNumberToObject(root, "seconds_since_boot", seconds_since_boot);
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(root, "wifi_rssi", ap_info.rssi);
    }

    float voltage = read_voltage();
    cJSON_AddNumberToObject(root, "voltage", voltage);
    
    // Add heading
    cJSON_AddNumberToObject(root, "heading", telemetry_values.heading);

    // Add latitude and longitude directly to root
    cJSON_AddNumberToObject(root, "latitude", telemetry_values.latitude);
    cJSON_AddNumberToObject(root, "longitude", telemetry_values.longitude);

    // Publish to stream
    char *json_str = cJSON_Print(root);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(root);
        return;
    }

    esp_err_t err = yak_api_publish("telemetry", "application/json", json_str, strlen(json_str));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish telemetry: %s", esp_err_to_name(err));
    }

    cJSON_Delete(root);
}

void handle_command_event(yak_stream_message_t *msg) 
{
    // Check for null message or data
    if (!msg || !msg->data) {
        ESP_LOGE(TAG, "Received null message or data");
        return;
    }

    // Validate string length
    size_t len = strlen(msg->data);
    if (len == 0 || len > 32) {
        ESP_LOGE(TAG, "Invalid data length: %d", len);
        return;
    }

    ESP_LOGI(TAG, "Received command: '%s' (len %d)", msg->data, len);

    // Check for valid numeric string (only digits and optional minus sign)
    for (size_t i = 0; i < len; i++) {
        if (i == 0 && msg->data[i] == '-') continue;
        if (!isdigit((unsigned char)msg->data[i])) {
            ESP_LOGE(TAG, "Invalid character in data: %c", msg->data[i]);
            return;
        }
    }

    // Convert string to integer
    int power = atoi(msg->data);

    // Bound check the power value
    if (power < 0 || power > 1) {
        ESP_LOGE(TAG, "Power value out of range: %d", power);
        return;
    }

    ESP_LOGI(TAG, "Set %s to %d", msg->stream_name, power);
    
    if (strcmp(msg->stream_name, "motor_a") == 0) {
        motor_a_set_power(power);
    } else if (strcmp(msg->stream_name, "motor_b") == 0) {
        motor_b_set_power(power);
    } else {
        ESP_LOGW(TAG, "unexpected stream %s", msg->stream_name);
    }
}

void telemetry_task(void *pvParameters) {
    while(1) {
        send_telemetry_stream();
        vTaskDelay(pdMS_TO_TICKS(10000));  // Or whatever interval you want
    }
}

void app_main()
{
    telemetry_values.heading = 0;
    telemetry_values.latitude = 47.816944;
    telemetry_values.longitude = -119.656111;

    initialise_wifi();
    camera_init();
    init_adc();
    motor_init();
    yak_api_init();
    
    // Create stream subscriptions
    xTaskCreate(yak_api_subscription_task, "motor_a", 4096, "motor_a", 5, NULL);
    xTaskCreate(yak_api_subscription_task, "motor_b", 4096, "motor_b", 5, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL);

    // Now main loop just handles commands
    while(1) {
        yak_stream_message_t msg;
        if (xQueueReceive(yak_api_get_queue(), &msg, pdMS_TO_TICKS(10000)) == pdTRUE) {
            handle_command_event(&msg);
            free(msg.data);
        }
    }
}