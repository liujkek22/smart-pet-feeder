#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
static  char *TAG = "webscoket_fron_logs";
void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}