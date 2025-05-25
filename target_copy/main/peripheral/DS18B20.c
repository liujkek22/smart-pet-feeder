#include "DS18B20.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"  // 新版延时函数头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 延时函数，延时单位为微秒
void Delay_DS18B20(int us)
{
    esp_rom_delay_us(us);
}

/*
* 函数名：Init_DS18B20
* 描  述：初始化 DS18B20 复位总线，产生复位脉冲
*         若 DS18B20 存在，则在释放总线后会拉低数据线
*/
void Init_DS18B20(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DQ_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,  // 开漏模式
        .pull_up_en = GPIO_PULLUP_ENABLE,    // 启用内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    unsigned char x = 0;

    gpio_set_level(DQ_PIN, 1);  // 拉高总线
    Delay_DS18B20(8);           // 延时 8us
    gpio_set_level(DQ_PIN, 0);  // 拉低总线，产生复位脉冲
    Delay_DS18B20(480);         // 延时至少 480us
    gpio_set_level(DQ_PIN, 1);  // 释放总线，拉高
    Delay_DS18B20(70);          // 等待 70us

    // 读取存在脉冲，若 DS18B20 存在则数据线会被拉低
    x = gpio_get_level(DQ_PIN);
    ESP_LOGI("DS18B20", "Initialization Status: %d", x);
    Delay_DS18B20(410);         // 剩余恢复延时，总时长约 960us
}

/*
* 函数名：Write18B20
* 描  述：向 DS18B20 写入一个字节数据
*/
void Write18B20(unsigned char dat)
{
    for (unsigned char i = 0; i < 8; i++)
    {
        // 开始写槽：拉低总线
        gpio_set_level(DQ_PIN, 0);
        if (dat & 0x01)
        {
            // 写 1：短暂低电平后释放总线
            Delay_DS18B20(2);    // 2us 左右
            gpio_set_level(DQ_PIN, 1);
            Delay_DS18B20(58);   // 完成整个写槽（约 60us）
        }
        else
        {
            // 写 0：保持低电平较长时间
            Delay_DS18B20(60);   // 低电平持续 60us
            gpio_set_level(DQ_PIN, 1);
            Delay_DS18B20(2);
        }
        dat >>= 1;
    }
}

/*
* 函数名：Read18B20
* 描  述：读取 DS18B20 的一个字节数据
*/
unsigned char Read18B20(void)
{
    unsigned char dat = 0;
    for (unsigned char i = 0; i < 8; i++)
    {
        // 开始读槽：拉低总线开始采样
        gpio_set_level(DQ_PIN, 0);
        Delay_DS18B20(2);
        gpio_set_level(DQ_PIN, 1);  // 释放总线，让 DS18B20 驱动数据线
        Delay_DS18B20(8);           // 等待 8us 后采样数据
        dat >>= 1;
        if (gpio_get_level(DQ_PIN))
        {
            dat |= 0x80;          // 若数据线高电平，置最高位
        }
        Delay_DS18B20(50);        // 等待槽结束，总时长约 60us
    }
    return dat;
}

/*
* 函数名：Get18B20Temp
* 描  述：读取 DS18B20 温度值，并启动下一次转换
*/
float Get18B20Temp(void)
{
    unsigned int Temp_L, Temp_H;
    unsigned int TempValue;
    float temperature = 0;

    // 复位并发送温度转换命令
    Init_DS18B20();
    Write18B20(0xCC);   // Skip ROM 命令
    Write18B20(0x44);   // 温度转换命令
    ESP_LOGI("DS18B20", "Temperature Conversion Started...");

    // 等待温度转换完成（750ms ~ 1000ms）
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // 复位并发送读取温度命令
    Init_DS18B20();
    Write18B20(0xCC);   // Skip ROM 命令
    Write18B20(0xBE);   // 读取温度命令

    Temp_L = Read18B20();   // 读取低字节
    Temp_H = Read18B20();   // 读取高字节

    TempValue = (Temp_H << 8) | Temp_L;
    ESP_LOGI("DS18B20", "Raw Temp: 0x%04X", TempValue);

    // DS18B20 分辨率为 0.0625℃/位，处理负温采用补码
    if (TempValue & 0xF800)  // 判断是否为负温（最高 5 位为1）
    {
        TempValue = (~TempValue) + 1;
        temperature = -((float)TempValue * 0.0625);
    }
    else
    {
        temperature = (float)TempValue * 0.0625;
    }
    return temperature;
}

/*
* 函数名：DS18B20Init
* 描  述：DS18B20 初始化函数，启动温度转换以备后续直接读取
*/
void DS18B20Init(void)
{
    Init_DS18B20();
    Write18B20(0xCC);  // Skip ROM 命令
    Write18B20(0x44);  // 启动温度转换
    esp_log_level_set("DS18B20", ESP_LOG_WARN);
}
