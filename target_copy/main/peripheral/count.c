#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "time.h"

#define TAG "FEED_SENSOR"


// ==== 用户可修改参数 ====
#define FEED_SENSOR_GPIO 40      // 传感器输出连接的 GPIO
#define FEED_ACTIVE_LEVEL 1               // 定义触发电平（1为高电平，0为低电平）
#define RESET_INTERVAL_SEC (24 * 60 * 60) // 每24小时重置一次
#define DEBOUNCE_MS 200                   // 简单消抖间隔
#define TASK_INTERVAL_MS 100              // 主循环检测周期

// ==== 内部变量 ====
static int feed_event_count = 0;
static time_t last_reset_time = 0;

static void feed_sensor_task(void *arg)
{
    int last_state = gpio_get_level(FEED_SENSOR_GPIO);
    time(&last_reset_time); // 记录起始时间
    ESP_LOGI(TAG, "Feed sensor input initialized on GPIO%d.", FEED_SENSOR_GPIO);
    while (1)
    {
        int current_state = gpio_get_level(FEED_SENSOR_GPIO);

        // 检测边沿：低 -> 高（或相反），代表喂食发生
        if (current_state == FEED_ACTIVE_LEVEL && last_state != current_state)
        {
            feed_event_count++;
            ESP_LOGW(TAG, "Feed Event Detected! Count = %d", feed_event_count);
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS)); // 简单防抖
        }

        last_state = current_state;

        // 判断是否到达重置时间
        time_t now;
        time(&now);
        if (difftime(now, last_reset_time) >= RESET_INTERVAL_SEC)
        {
            ESP_LOGI(TAG, "24h elapsed. Resetting feed count.");
            feed_event_count = 0;
            last_reset_time = now;
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_INTERVAL_MS));
    }
}

void feed_count_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << FEED_SENSOR_GPIO),
        .pull_up_en = 1, // 根据传感器需要配置上下拉
        .pull_down_en = 0};
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Feed sensor input initialized on GPIO%d.", FEED_SENSOR_GPIO);
    xTaskCreate(feed_sensor_task, "feed_sensor_task", 4096, NULL, 5, NULL);
}
int get_count(void)
{
    return feed_event_count;
}