#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define GPIO_OUTPUT GPIO_NUM_45
#define GPIO_OUTPUT2 GPIO_NUM_38
void water_ctrol_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT) | (1ULL << GPIO_OUTPUT2); // 修正位掩码
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
}

void water_ctrol_do(void)
{
    gpio_set_level(GPIO_OUTPUT, 1);
    ESP_LOGI("water_ctrol_do", "GPIO45 set high");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_OUTPUT, 0);
    ESP_LOGI("water_ctrol_do", "GPIO45 set low");

    gpio_set_level(GPIO_OUTPUT2, 1);
    ESP_LOGI("water_ctrol_do", "GPIO38 set high");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_OUTPUT2, 0);
    ESP_LOGI("water_ctrol_do", "GPIO38 set low");

}