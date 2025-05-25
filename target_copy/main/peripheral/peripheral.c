
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp32s3/rom/ets_sys.h"

#define warm_gpio 25   // 加热器引脚


#define HX711_DOUT_GPIO GPIO_NUM_2 // 数据引脚
#define HX711_SCK_GPIO GPIO_NUM_41 // 时钟引脚
#define TAG "HX711"

static int32_t weight_cache = 0;
#define HX711_RAW_ZERO 8100000
#define HX711_RAW_MAX 9334052
#define HX711_KG_PER_UNIT (2.9f / (HX711_RAW_MAX - HX711_RAW_ZERO))
static uint32_t hx711_read_raw(void)
{
    uint32_t count = 0;
    gpio_set_level(HX711_SCK_GPIO, 0);
    while (gpio_get_level(HX711_DOUT_GPIO))
        ; // 等待数据就绪

    for (int i = 0; i < 24; i++)
    {
        gpio_set_level(HX711_SCK_GPIO, 1);
        count <<= 1;
        gpio_set_level(HX711_SCK_GPIO, 0);
        if (gpio_get_level(HX711_DOUT_GPIO))
        {
            count++;
        }
    }
    // 25th pulse
    gpio_set_level(HX711_SCK_GPIO, 1);
    count ^= 0x800000;
    gpio_set_level(HX711_SCK_GPIO, 0);
    return count;
}

static void hx711_task(void *arg)
{
    while (1)
    {
        weight_cache = hx711_read_raw();
        ESP_LOGI(TAG, "weight raw: %ld", weight_cache);
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000)); // 30 间隔
    }
}

void hx711_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << HX711_SCK_GPIO),
    };
    gpio_config(&io_conf);
    gpio_set_level(HX711_SCK_GPIO, 0);

    gpio_config_t io_conf_in = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << HX711_DOUT_GPIO),
        .pull_up_en = 1,
    };
    gpio_config(&io_conf_in);

    xTaskCreate(hx711_task, "hx711_task", 2048, NULL, 5, NULL);
}

int hx711_get_weight(void)
{
    if (weight_cache <= HX711_RAW_ZERO)
        return 0;
    return (weight_cache - HX711_RAW_ZERO) * HX711_KG_PER_UNIT;
}
// 加热器控制
void warm_init(){
    // 初始化GPIO
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << warm_gpio),
    };
    gpio_config(&io_conf);
    gpio_set_level(warm_gpio, 0);   // 默认关闭加热器
}
void warm_on()
{
    gpio_set_level(warm_gpio, 1); // 打开加热器
}
void warm_off()
{
    gpio_set_level(warm_gpio, 0); // 关闭加热器
}