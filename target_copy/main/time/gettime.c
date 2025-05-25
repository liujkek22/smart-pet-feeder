#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "gettime.h"
#include "stdbool.h"
// 闹钟系统
#define MAX_ALARMS 3



// 存储闹钟数据
Alarm alarms[MAX_ALARMS];

// FreeRTOS定时器句柄
TimerHandle_t alarm_timer = NULL;
int current_alarm = 0; // 当前正在触发的闹钟编号
static const char *TAG = "RTC-time";
static void obtain_time(void);

void init_alarm_system()
{
    for (int i = 0; i < MAX_ALARMS; i++)
    {
        alarms[i].hour = -1;
        alarms[i].minute = -1;
        alarms[i].is_set = 0;
    }
    current_alarm = 0; // 从第一个闹钟开始
}
void set_alarm(int alarm_id, int hour, int minute) {
    if (alarm_id < 0 || alarm_id >= MAX_ALARMS) {
        ESP_LOGI("ALARM", "Invalid alarm ID.");
        return;
    }
    alarms[alarm_id].hour = hour;
    alarms[alarm_id].minute = minute;
    alarms[alarm_id].is_set = 1;
    ESP_LOGI("ALARM", "Alarm %d set to %02d:%02d", alarm_id, hour, minute);
}

int get_seconds_until_alarm(int alarm_id)
{
    if (alarms[alarm_id].is_set == 0)
    {
        return -1; // 闹钟未设置
    }

    // 获取当前时间
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 当前时间与闹钟时间差
    int current_hour = timeinfo.tm_hour;
    int current_minute = timeinfo.tm_min;
    int target_hour = alarms[alarm_id].hour;
    int target_minute = alarms[alarm_id].minute;

    int current_time_in_seconds = current_hour * 3600 + current_minute * 60;
    int target_time_in_seconds = target_hour * 3600 + target_minute * 60;

    // 如果目标时间早于当前时间，认为是第二天的时间
    if (target_time_in_seconds < current_time_in_seconds)
    {
        target_time_in_seconds += 24 * 3600;
    }

    return target_time_in_seconds - current_time_in_seconds;
}
void alarm_callback(TimerHandle_t xTimer)
{
    if (alarms[current_alarm].is_set)
    {
        ESP_LOGI("ALARM", "Alarm %d triggered! Time: %02d:%02d", current_alarm, alarms[current_alarm].hour, alarms[current_alarm].minute);
        // 在这里可以执行闹钟触发时的操作，例如响铃、点亮LED等

        // 触发下一个闹钟
        current_alarm++;
        if (current_alarm >= MAX_ALARMS)
        {
            current_alarm = 0; // 如果到了最后一个闹钟，重新从第一个闹钟开始
        }

        // 设置下一个定时器
        int delay_seconds = get_seconds_until_alarm(current_alarm);
        xTimerChangePeriod(alarm_timer, pdMS_TO_TICKS(delay_seconds * 1000), 0);
    }
}
void start_alarm_system()
{
    // 创建一个定时器，定时器会周期性地检查并触发下一个闹钟
    alarm_timer = xTimerCreate("alarm_timer", pdMS_TO_TICKS(1000), pdFALSE, (void *)0, alarm_callback);
    if (alarm_timer == NULL)
    {
        ESP_LOGI("ALARM", "Failed to create timer.");
        return;
    }

    // 启动定时器
    int delay_seconds = get_seconds_until_alarm(current_alarm);
    xTimerChangePeriod(alarm_timer, pdMS_TO_TICKS(delay_seconds * 1000), 0);
    xTimerStart(alarm_timer, 0); // 启动定时器
}

// 时间同步回调
void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    settimeofday(tv, NULL); // 同步系统时间
}

// 对外函数，初始化 SNTP 并同步时间
void rtc_sntp_time_main(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 检查时间是否已设置
    if (timeinfo.tm_year < (2016 - 1900))
    {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        time(&now);
    }
    else
    {
        ESP_LOGI(TAG, "Time was set, now adjusting it.");
        obtain_time(); // 同步时间
        time(&now);
    }

    // 设置时区为中国标准时间
    setenv("TZ", "CST-8", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);
    vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
    // 再次获取并打印当前时间
    // time(&now);  // 重新获取当前时间戳
    // localtime_r(&now, &timeinfo);  // 更新 timeinfo

    // strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    // ESP_LOGI(TAG, "After 10s The current date/time in Shanghai is: %s", strftime_buf);
    
}
// esp_err_t alarm_set(Alarm *alarm,int alarm_count){
//     ESP_LOGW(TAG, "time alarm init and start");
//     // 初始化闹钟系统
//     init_alarm_system();
//     for (int i = 0; i < alarm_count; i++)
//     {
//         set_alarm(i, alarm[i].hour, alarm[i].minute);
//     }
//     // 启动闹钟系统
//     start_alarm_system();
//     return ESP_OK;
// }

// esp_err_t alarm_set_change(Alarm *alarm, int alarm_count) {
//     ESP_LOGW(TAG, "time alarm change and start");

//     // 停止当前的定时器任务
//     if (alarm_timer != NULL) {
//         xTimerStop(alarm_timer, 0);
//         ESP_LOGI(TAG, "Timer stopped.");
//     }

//     // 遍历所有闹钟并检查有效性
//     bool valid_alarm_found = false;  // 使用 bool 类型标记是否找到有效的闹钟
//     for (int i = 0; i < alarm_count; i++) {
//         if (alarm[i].is_set == 1) {
//             // 更新闹钟时间
//             alarms[i].hour = alarm[i].hour;
//             alarms[i].minute = alarm[i].minute;

//             // 计算下一个有效的闹钟
//             int delay_seconds = get_seconds_until_alarm(i);
//             if (delay_seconds > 0) {
//                 current_alarm = i;  // 找到第一个有效的闹钟
//                 valid_alarm_found = true;  // 标记为找到有效的闹钟
//                 break;  // 找到有效的闹钟后停止循环
//             }
//         }
//     }

//     // 如果没有找到有效的闹钟（说明所有闹钟时间已经过了），重置 current_alarm 为 0，表示从明天开始
//     if (!valid_alarm_found) {
//         ESP_LOGI(TAG, "No valid alarm found, starting from tomorrow.");
//         current_alarm = 0;  // 从第一个闹钟开始，明天的闹钟      
//     }
//     ESP_LOGI(TAG, "Next alarm: %d", current_alarm);
//     // 重新启动定时器任务
//     int delay_seconds = get_seconds_until_alarm(current_alarm);
//     xTimerChangePeriod(alarm_timer, pdMS_TO_TICKS(delay_seconds * 1000), 0);
//     xTimerStart(alarm_timer, 0);

//     return ESP_OK;
// }

// 获取时间并设置 SNTP 配置
static void obtain_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.sync_cb = time_sync_notification_cb; // 设置同步回调
    config.smooth_sync = true;                  // 启用平滑同步
    esp_netif_sntp_init(&config);

    // 等待时间同步
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    if (retry == retry_count)
    {
        ESP_LOGE(TAG, "Failed to synchronize time after %d retries", retry_count);
    }

    time(&now);
    localtime_r(&now, &timeinfo);
}
