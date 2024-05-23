#include "motor.h"
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "MOTOR";

void motor_init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = MOTOR_A_PIN_SEL | MOTOR_M_PIN_SEL,
       .mode = GPIO_MODE_OUTPUT,
       .pull_up_en = GPIO_PULLUP_DISABLE,
       .pull_down_en = GPIO_PULLDOWN_DISABLE,
       .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Motor Init Complete");
}

void _motor_a_set_direction(int direction) {
    if (direction > 0) {
        gpio_set_level(MOTOR_PIN_A_1A, 1);
        gpio_set_level(MOTOR_PIN_A_2A, 0);
    } else {
        gpio_set_level(MOTOR_PIN_A_1A, 0);
        gpio_set_level(MOTOR_PIN_A_2A, 1);
    }
}

void _motor_b_set_direction(int direction) {
    if (direction > 0) {
        gpio_set_level(MOTOR_PIN_A_3A, 0);
        gpio_set_level(MOTOR_PIN_A_4A, 1);
    } else {
        gpio_set_level(MOTOR_PIN_A_3A, 1);
        gpio_set_level(MOTOR_PIN_A_4A, 0);
    }
}

void motor_stop() {
    gpio_set_level(MOTOR_PIN_M1_EN, 0);
    gpio_set_level(MOTOR_PIN_M3_EN, 0);
    gpio_set_level(MOTOR_PIN_M2_EN, 0);
    gpio_set_level(MOTOR_PIN_M4_EN, 0);
}

void motor_forward(int speed, int time) {
    _motor_a_set_direction(1);
    _motor_b_set_direction(1);

    gpio_set_level(MOTOR_PIN_M1_EN, 1);
    gpio_set_level(MOTOR_PIN_M3_EN, 1);
    gpio_set_level(MOTOR_PIN_M2_EN, 1);
    gpio_set_level(MOTOR_PIN_M4_EN, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    motor_stop();
}

void motor_backward(int speed, int time) {
    _motor_a_set_direction(-1);
    _motor_b_set_direction(-1);

    gpio_set_level(MOTOR_PIN_M1_EN, 1);
    gpio_set_level(MOTOR_PIN_M3_EN, 1);
    gpio_set_level(MOTOR_PIN_M2_EN, 1);
    gpio_set_level(MOTOR_PIN_M4_EN, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    motor_stop();
}

void motor_left(double angle) {
    _motor_a_set_direction(-1);
    _motor_b_set_direction(1);

    gpio_set_level(MOTOR_PIN_M1_EN, 1);
    gpio_set_level(MOTOR_PIN_M3_EN, 1);
    gpio_set_level(MOTOR_PIN_M2_EN, 1);
    gpio_set_level(MOTOR_PIN_M4_EN, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    motor_stop();
}

void motor_right(double angle) {
    _motor_a_set_direction(1);
    _motor_b_set_direction(-1);

    gpio_set_level(MOTOR_PIN_M1_EN, 1);
    gpio_set_level(MOTOR_PIN_M3_EN, 1);
    gpio_set_level(MOTOR_PIN_M2_EN, 1);
    gpio_set_level(MOTOR_PIN_M4_EN, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    motor_stop();
}