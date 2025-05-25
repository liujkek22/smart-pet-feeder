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
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "logs.h"
#include <cJSON.h>
#include "DS18B20.h"
#include "servo.h"
#include "waterctrol.h"
#include "level.h"
#include "count.h"
#include "peripheral.h"
#define NO_DATA_TIMEOUT_SEC 5
// 上载信息信号量
SemaphoreHandle_t xSemaphore_upload = NULL;
SemaphoreHandle_t xSemaphore = NULL;
// 定时任务的定时任务
TimerHandle_t xTimers = NULL;
// TAG
static const char *TAG = "websocket";
//  产品UUID设置
const char *UUID = "123e4567-e89b-12d3-a456-426614174000";
//  消息队列
// 定义队列句柄
QueueHandle_t websocket_queue;
// 客户端全局变量
esp_websocket_client_handle_t client = NULL;
// 定义数据结构
typedef struct
{
    char data[256]; // 假设每条消息最大为 256 字节
} websocket_data_t;
// 使用静态变量来避免每次创建新变量
static websocket_data_t received_data;
// 恒温系统温度值
int temperature = 0;
// ---------------------------------------------------分割线
#define HEATER_GPIO 25      // 你的加热模块控制引脚
#define TEMP_TOLERANCE 0.5f // 控制阈值：±0.5°C
TaskHandle_t temp_control_task_handle = NULL;

//  任务执行-----------------------------------------------
// 命令类型枚举
typedef enum
{
    CMD_REGISTER_LOAD,
    CMD_WATER,
    CMD_FEED,
    CMD_WARM_ON,
    CMD_WARM_OFF
} CommandType;

// 命令消息结构体
typedef struct
{
    CommandType cmd;
    int temperature; // 仅 CMD_WARM_ON 使用
} CommandMsg;

QueueHandle_t cmd_queue;
void temperature_control_task(void *pvParameter)
{
    bool heater_on = false;
    while (true)
    {
        // 等待外部通知开启（第一次运行或恢复后运行）
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // 等待“开启”信号
        ESP_LOGE("TEMP", "Temperature Control Started");
        while (1)
        {
            // 恒温控制逻辑
            float temp = Get18B20Temp();
            if (temp < temperature - TEMP_TOLERANCE)
            {
                if (!heater_on)
                {
                    heater_on = true;
                    ESP_LOGI("TEMP", "Heater ON: Temp=%.2f", temp);
                }
            }
            else if (temp > temperature + TEMP_TOLERANCE)
            {
                if (heater_on)
                {
                    // gpio_set_level(HEATER_GPIO, 0); // 关闭加热器
                    heater_on = false;
                    ESP_LOGI("TEMP", "Heater OFF: Temp=%.2f", temp);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(2000)); // 每 2 秒判断一次
        }
    }
}
void start_temperature_control()
{
    if (temp_control_task_handle != NULL)
    {
        vTaskResume(temp_control_task_handle);
        xTaskNotifyGive(temp_control_task_handle);  // 启动控制循环
    }
}

void stop_temperature_control()
{
    if (temp_control_task_handle != NULL)
    {
        // gpio_set_level(HEATER_GPIO, 0);  // 确保加热关闭
        vTaskSuspend(temp_control_task_handle);     // 挂起任务
    }
}


void safe_copy_json(char *dest, size_t dest_size, const char *src, size_t src_len)
{
    if (dest == NULL || src == NULL || dest_size == 0)
        return;

    // 限制最大拷贝长度，保留一个位置用于 '\0'
    size_t copy_len = (src_len < dest_size - 1) ? src_len : dest_size - 1;

    // 清空目标缓冲区（可选，更干净）
    memset(dest, 0, dest_size);

    // 拷贝有效数据
    memcpy(dest, src, copy_len);

    // 手动添加字符串结束符
    dest[copy_len] = '\0';
}
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id)
    {
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        // 释放已经连接的信号
        xSemaphoreGive(xSemaphore);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        // ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x2)
        { // Opcode 0x2 indicates binary data
            ESP_LOG_BUFFER_HEX("Received binary data", data->data_ptr, data->data_len);
        }
        else if (data->op_code == 0x08 && data->data_len == 2)
        {
            ESP_LOGW(TAG, "Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
        }
        else if (data->op_code == 0x01 && data->data_len > 0 && data->data_ptr != NULL)
        {
            ESP_LOGW(TAG, "Received JSON string: %.*s", data->data_len, (char *)data->data_ptr);
            //  清空数据，但是实际这里会处出现问题
            // memset(received_data.data, 0, sizeof(received_data.data));
            // memcpy(received_data.data, data->data_ptr, sizeof(received_data.data) - 1);
            // received_data.data[sizeof(received_data.data) - 1] = '\0';
            // ✅ 投递进队列
            safe_copy_json(received_data.data, sizeof(received_data.data), (char *)data->data_ptr, data->data_len);
            if (xQueueSend(websocket_queue, &received_data, portMAX_DELAY) != pdPASS)
            {
                ESP_LOGE(TAG, "Failed to send data to websocket_queue");
            }
        }
        else if (data->op_code == 0xa)
        {
            ESP_LOGW(TAG, "Received pong string: %.*s", data->data_len, (char *)data->data_ptr);
            ESP_LOGI("TEMP", "Stack watermark: %d", uxTaskGetStackHighWaterMark(NULL));

        }
        else
        {
            ESP_LOGD(TAG, "Received unknown opcode=%d", data->op_code);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        break;
    }
}

void websocket_event_task(void *pvParameter)
{
    ESP_LOGW(TAG, "Websocket event task start");
    while (true)
    {
        // 创建局部变量接受收到的数据
        websocket_data_t websocket_data;
        if (xQueueReceive(websocket_queue, &websocket_data, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGW(TAG, "Websocket event task receive data");

            // 检查数据是否为空（检查第一个字符是否为空）
            if (websocket_data.data[0] == '\0')
            {
                ESP_LOGW(TAG, "Received data is empty");
                continue; // 跳过这次处理
            }

            // 数据输出，方便查看
            ESP_LOGI(TAG, "Websocket event task receive data: %s", websocket_data.data);

            // 解析 json 数据
            cJSON *root = cJSON_Parse(websocket_data.data);
            if (root == NULL)
            {
                ESP_LOGW(TAG, "Websocket event task parse json error");
                continue; // 如果解析失败，跳过这次处理
            }
            else
            {
                ESP_LOGI(TAG, "Websocket event task receive data parse success");
                CommandMsg msg = {}; // 命令解析队列
                // 获取 "action" 字段
                cJSON *action_item = cJSON_GetObjectItem(root, "action");
                if (action_item == NULL || action_item->valuestring == NULL)
                {
                    ESP_LOGW(TAG, "Action field is missing or invalid");
                    cJSON_Delete(root); // 及时释放内存
                    continue;
                }

                // 输出 action_item 的值
                ESP_LOGI(TAG, "Action: %s", action_item->valuestring);

                // 根据 action 字段判断不同操作
                if (strcmp(action_item->valuestring, "register load") == 0)
                {
                    ESP_LOGI(TAG, "Websocket event task receive register load");
                    msg.cmd = CMD_REGISTER_LOAD;
                }
                else if (strcmp(action_item->valuestring, "Water") == 0)
                {
                    ESP_LOGI(TAG, "Websocket event task receive water");
                    msg.cmd = CMD_WATER;
                }
                else if (strcmp(action_item->valuestring, "feed") == 0)
                {
                    ESP_LOGI(TAG, "Websocket event task receive time alarm");
                    msg.cmd = CMD_FEED;
                }
                else if (strcmp(action_item->valuestring, "warm_on") == 0)
                {
                    ESP_LOGI(TAG, "warm on");
                    msg.cmd = CMD_WARM_ON;
                    cJSON *temperature_json = cJSON_GetObjectItem(root, "temperature");
                    if (temperature_json != NULL && temperature_json->valueint != 0)
                    {
                        ESP_LOGI(TAG, "temperature: %d", temperature_json->valueint);
                        // 获取到温度值，进行后续处理
                        msg.temperature = temperature_json->valueint;
                    }
                    // 解析warm 系统需要的温度恒定值
                }
                else if (strcmp(action_item->valuestring, "warm_off") == 0)
                {
                    ESP_LOGI(TAG, "warm  off");
                    msg.cmd = CMD_WARM_OFF;
                }
                else
                {
                    ESP_LOGI(TAG, "Websocket event task receive unknown action");
                }
                xQueueSend(cmd_queue, &msg, portMAX_DELAY);
                // 解析完成后释放 JSON 内存
                cJSON_Delete(root);
            }

            // 模拟数据处理
            vTaskDelay(500 / portTICK_PERIOD_MS); // 假设处理数据需要一些时间
        }

        // 防止死机
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
// 硬件注册
static void hw_init()
{

    // 温度读取初始化
    DS18B20Init();
    // 舵机初始化
    servo_init();
    // ADC读取初始化->water-level sensor
    level_sensor_init();
    // feed count -> 记录来吃食物的次数
    feed_count_init();
    // 初始化water control
    water_ctrol_init();
    // 初始化食物重量计数
    hx711_init();
    // 加热器启动初始化
    warm_init();
}

// 注册任务
void register_server_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Register server task start");
    while (1)
    {
        // 如果收到连接成功的信号量,第一次发送注册的信息
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGW(TAG, "Websocket event task receive connect success signal");
            //  发送注册信息
            //  创建注册json,包括register+UUID身份识别
            // esp_websocket_client_send_text(client, register_json, strlen(register_json), portMAX_DELAY);// 发送注册信息
            // 创建注册 JSON，包含 register 和 UUID 身份识别
            cJSON *register_json = cJSON_CreateObject();
            if (register_json == NULL)
            {
                ESP_LOGE(TAG, "Failed to create JSON object");
                continue;
            }

            // 添加 register 动作
            cJSON_AddStringToObject(register_json, "action", "register");
            cJSON_AddStringToObject(register_json, "UUID", UUID);

            // 将 JSON 对象转换为字符串
            char *register_json_str = cJSON_PrintUnformatted(register_json);
            if (register_json_str == NULL)
            {
                ESP_LOGE(TAG, "Failed to print JSON string");
                cJSON_Delete(register_json); // 释放 JSON 对象
                continue;
            }

            // 发送注册 JSON 数据
            if (esp_websocket_client_send_text(client, register_json_str, strlen(register_json_str), portMAX_DELAY) == -1)
            {
                ESP_LOGE(TAG, "Failed to send WebSocket register message");
            }
            else
            {
                ESP_LOGI(TAG, "WebSocket register message sent: %s", register_json_str);
            }
            // 释放资源
            cJSON_Delete(register_json); // 删除 JSON 对象
            free(register_json_str);     // 释放 JSON 字符串
            // 删除任务
            vTaskDelete(NULL);
        }
    }
}
void vTimerCallback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Timer expired");
    xSemaphoreGive(xSemaphore_upload);
}

// 定时任务
void data_process_task(void *pvParameter)
{
    // TODO: replace fixed values with actual sensor readings
    ESP_LOGW(TAG, "Websocket data process task start");
    while (true)
    {
        if (xSemaphoreTake(xSemaphore_upload, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGW(TAG, "Websocket data process task receive upload signal");

            // 创建一个 JSON 对象
            cJSON *upload_json = cJSON_CreateObject();
            if (upload_json == NULL)
            {
                ESP_LOGE(TAG, "Failed to create JSON object");
                continue;
            }
            // 获取数据

            // 填充 JSON 对象
            cJSON_AddStringToObject(upload_json, "UUID", UUID);                                    // UUID
            cJSON_AddStringToObject(upload_json, "action", "upload");                              // action
            cJSON_AddNumberToObject(upload_json, "water_level", level_sensor_read_once_percent()); // 水位
            cJSON_AddNumberToObject(upload_json, "water_temperature", Get18B20Temp());             // 水温
            cJSON_AddNumberToObject(upload_json, "feed_count", get_count());                       // 投喂次数
            cJSON_AddStringToObject(upload_json, "status", "online");                              // 状态
            cJSON_AddNumberToObject(upload_json, "food_weight", hx711_get_weight());
            // 将 JSON 对象转换为字符串
            char *upload_json_str = cJSON_PrintUnformatted(upload_json);
            if (upload_json_str == NULL)
            {
                ESP_LOGE(TAG, "Failed to convert JSON to string");
                cJSON_Delete(upload_json); // 释放 JSON 对象内存
                continue;
            }

            // 通过 WebSocket 发送数据
            ESP_LOGI(TAG, "Sending WebSocket data: %s", upload_json_str);
            // 通过 WebSocket 发送数据
            if (esp_websocket_client_send_text(client, upload_json_str, strlen(upload_json_str), portMAX_DELAY) == -1)
            {
                ESP_LOGE(TAG, "Failed to send WebSocket register message");
            }
            else
            {
                ESP_LOGI(TAG, "WebSocket register message sent: %s", upload_json_str);
            }

            // 释放 JSON 对象和字符串内存
            cJSON_Delete(upload_json);
            free(upload_json_str); // 释放 JSON 字符串内存
        }

        // 防止死机，适当的延时
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void feed_task(void)
{

    ESP_LOGE(TAG, "Websocket feed task receive upload signal and first alarm");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    servo_angle_set(90);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    servo_angle_set(-90);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    servo_angle_set(0);
}

void command_dispatch_task(void *pvParam)
{
    CommandMsg msg;
    while (xQueueReceive(cmd_queue, &msg, portMAX_DELAY))
    {
        switch (msg.cmd)
        {
        case CMD_REGISTER_LOAD:
            ESP_LOGE(TAG, "注册加载触发");
            xSemaphoreGive(xSemaphore_upload);
            break;
        case CMD_WATER:
            ESP_LOGE(TAG, "水控触发");
            water_ctrol_do();
            break;
        case CMD_FEED:
            ESP_LOGE(TAG, "投喂触发");
            feed_task();
            break;
        case CMD_WARM_ON:
            temperature = msg.temperature;
            ESP_LOGW(TAG, "水温控制触发");
            start_temperature_control();
            break;
        case CMD_WARM_OFF:
            stop_temperature_control();
            ESP_LOGE(TAG, "水温控制关闭");
            break;
        default:
            ESP_LOGW(TAG, "未知命令");
            break;
        }
    }
}
// -------------------------------------------
void websocket_app_start(void)
{
    ESP_LOGW(TAG, "Websocket init start");
    // 创建队列，队列长度为 10，每条消息大小为 websocket_data_t 类型
    websocket_queue = xQueueCreate(10, sizeof(websocket_data_t));
    if (websocket_queue == NULL)
    {
        ESP_LOGE("Main", "队列解析错误");
        return;
    }
    cmd_queue = xQueueCreate(10, sizeof(websocket_data_t));
    if (!cmd_queue)
    {
        ESP_LOGE(TAG, "命令执行队列错误");
        return;
    }
    xSemaphore_upload = xSemaphoreCreateBinary();
    if (xSemaphore_upload == NULL)
    {
        ESP_LOGE(TAG, "上载信号量错误");
    }
    xSemaphore = xSemaphoreCreateBinary();
    if (xSemaphore == NULL)
    {
        ESP_LOGE(TAG, "信号量错误");
    }
    esp_websocket_client_config_t websocket_cfg = {};
    // webscoket 服务器地址
    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;
#if CONFIG_WS_OVER_TLS_MUTUAL_AUTH
    /* Configuring client certificates for mutual authentification */
    extern const char cacert_start[] asm("_binary_ca_cert_pem_start");   // CA certificate
    extern const char cert_start[] asm("_binary_client_cert_pem_start"); // Client certificate
    extern const char cert_end[] asm("_binary_client_cert_pem_end");
    extern const char key_start[] asm("_binary_client_key_pem_start"); // Client private key
    extern const char key_end[] asm("_binary_client_key_pem_end");

    websocket_cfg.cert_pem = cacert_start;
    websocket_cfg.client_cert = cert_start;
    websocket_cfg.client_cert_len = cert_end - cert_start;
    websocket_cfg.client_key = key_start;
    websocket_cfg.client_key_len = key_end - key_start;
#elif CONFIG_WS_OVER_TLS_SERVER_AUTH
    // Using certificate bundle as default server certificate source
    websocket_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    // If using a custom certificate it could be added to certificate bundle, added to the build similar to client certificates in this examples,
    // or read from NVS.
    /* extern const char cacert_start[] asm("ADDED_CERTIFICATE"); */
    /* websocket_cfg.cert_pem = cacert_start; */
#endif

#if CONFIG_WS_OVER_TLS_SKIP_COMMON_NAME_CHECK
    websocket_cfg.skip_cert_common_name_check = true;
#endif
    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);
    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    ESP_LOGW(TAG, "Websocket init over");
    /*
    title:启动各项硬件初始化
    1.初始化舵机控制->控制食物的进出
    2.初始化DS18B20温度读取->水的温度
    3.初始化ADC读取->水位的余量，方便加水
    4.初始化feed-count->记录宠物来进食次数
    */
    hw_init();
    // 启动事件处理任务
    xTaskCreate(&websocket_event_task, "websocket_event_task", 4096, NULL, 5, NULL);
    xTaskCreate(command_dispatch_task, "command_dispatch_task", 4096, NULL, 5, NULL);
    // 启动注册服务器的功能
    xTaskCreate(&register_server_task, "register_server_task", 4096, NULL, 5, NULL);
    //  启动数据处理任务
    xTaskCreate(&data_process_task, "data_process_task", 4096, NULL, 5, NULL);
    // // 恒温系统
    xTaskCreate(&temperature_control_task, "temperature_control_task", 4096, NULL, 5, NULL);
    xTimers = xTimerCreate(/* Just a text name, not used by the RTOS kernel. */
                           "Timer",
                           /* The timer period in ticks, must be greater than 0. */
                           pdMS_TO_TICKS(10 * 60 * 1000),
                           /* The timers will auto-reload themselves when they expire. */
                           pdTRUE,
                           /* The ID is used to store a count of the number of times the
                              timer has expired, which is initialised to 0. */
                           (void *)0,
                           /* Each timer calls the same callback when it expires. */
                           vTimerCallback);
    if (xTimers == NULL)
    {
        ESP_LOGW("Main", "Timer create failed");
    }
    else
    {
        xTimerStart(xTimers, 0);
    }
}