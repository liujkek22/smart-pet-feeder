// level.c
#include "level.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <inttypes.h>
#include "soc/adc_channel.h"
#define TAG "water_level"
#define MAX_VOLTAGE_MV 1100 // 实测值更合理

#define LEVEL_ADC_UNIT ADC_UNIT_1
#define LEVEL_ADC_CHANNEL ADC1_GPIO3_CHANNEL // GPIO3
#define LEVEL_ADC_ATTEN ADC_ATTEN_DB_11
#define LEVEL_ADC_BIT_WIDTH ADC_BITWIDTH_DEFAULT

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;

void level_sensor_init(void)
{
    if (adc_handle == NULL)
    {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = LEVEL_ADC_UNIT,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

        adc_oneshot_chan_cfg_t config = {
            .atten = ADC_ATTEN_DB_0,
            .bitwidth = LEVEL_ADC_BIT_WIDTH,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LEVEL_ADC_CHANNEL, &config));
    }

    if (cali_handle == NULL)
    {
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = LEVEL_ADC_UNIT,
            .chan = LEVEL_ADC_CHANNEL,
            .atten = LEVEL_ADC_ATTEN,
            .bitwidth = LEVEL_ADC_BIT_WIDTH,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK)
        {
            ESP_LOGI(TAG, "ADC calibration enabled.");
        }
        else
        {
            ESP_LOGW(TAG, "ADC calibration not supported. Raw voltage will be used.");
        }
    }

    ESP_LOGI(TAG, "Level sensor initialized (lazy-init) in oneshot mode on GPIO3");
}

int level_sensor_read_once_percent(void)
{
    if (adc_handle == NULL || cali_handle == NULL)
    {
        level_sensor_init();
    }

    int adc_raw = 0;
    int adc_sum = 0;
    for (int i = 0; i < 10; i++)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, LEVEL_ADC_CHANNEL, &adc_raw));
        adc_sum += adc_raw;
    }
    int adc_avg = adc_sum / 10;

    int voltage_mv = 0;
    if (cali_handle)
    {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_avg, &voltage_mv));
    }
    else
    {
        voltage_mv = (adc_avg * 1100) / 4095;
    }

    if (voltage_mv > MAX_VOLTAGE_MV)
        voltage_mv = MAX_VOLTAGE_MV;
    int percent = (voltage_mv * 100) / MAX_VOLTAGE_MV;

    ESP_LOGI(TAG, "[OneShotAvg] RawAvg: %d, Voltage: %d mV, Water Level: %d%%",
             adc_avg, voltage_mv, percent);

    return percent;
}
