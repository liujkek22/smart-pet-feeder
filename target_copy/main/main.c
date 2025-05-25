#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "webscoket.h"
#include "blufi_example.h"
#include "gettime.h"
#include "st7789.h"
#include "ui.h"
#include "led_strip.h"
#include "driver/gpio.h"
#include "DS18B20.h"
#include "servo.h"
#include "peripheral.h"

#define LED_STRIP_GPIO 4
#define LED_STRIP_LED_NUM 1
static const char *TAG = "main";

led_strip_handle_t led_strip;
static bool wifi_connected = false;
static bool websocket_connected = false;

void led_strip_init()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }};

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        }};

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

void led_status_task(void *pvParameter)
{
    int brightness = 0;
    int direction = 5;

    while (true)
    {
        if (!wifi_connected)
        {
            led_strip_set_pixel(led_strip, 0, 255, 0, 0);
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(500));
            led_strip_set_pixel(led_strip, 0, 0, 0, 0);
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        else if (wifi_connected && !websocket_connected)
        {
            led_strip_set_pixel(led_strip, 0, 0, 255, 0);
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            led_strip_set_pixel(led_strip, 0, 0, 0, brightness);
            led_strip_refresh(led_strip);
            brightness += direction;
            if (brightness >= 255 || brightness <= 0)
            {
                direction = -direction;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
}

void main_task(void *pvParameter)
{
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & CONNECTED_BIT)
    {
        wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected!");

        ESP_LOGI(TAG, "[APP] get time start!");
        rtc_sntp_time_main();

        ESP_LOGI(TAG, "[APP] LVGL start!");
        st7789_init();
        ui_init();

        ESP_LOGI(TAG, "[APP] websocket_app_start!");
        websocket_app_start();
        ESP_LOGI(TAG, "[APP] initial ok!");
        websocket_connected = true;
    }
    else
    {
        ESP_LOGI(TAG, "Wi-Fi failed!");
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    blufi_main();
    led_strip_init();
    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
    xTaskCreate(led_status_task, "led_status_task", 2048, NULL, 3, NULL);
    
}

// 注意：你需要在 websocket 连接成功的回调函数中加上：
// websocket_connected = true; // 表示连接成功
