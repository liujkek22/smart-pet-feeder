


#include"esp_err.h"
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    int hour;
    int minute;
    int is_set;
} Alarm;
void rtc_sntp_time_main(void);
// esp_err_t alarm_set(Alarm *alarm,int alarm_count);
// esp_err_t alarm_set_change(Alarm *alarm, int alarm_count);
#ifdef __cplusplus
}
#endif