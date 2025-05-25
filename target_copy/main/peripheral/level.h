#ifndef LEVEL_H
#define LEVEL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"{
#endif

    // 初始化 ADC 水位传感器
    void level_sensor_init(void);
    // 读取水位百分比（0-100%），基于多次平均采样
    int level_sensor_read_once_percent(void);

#ifdef __cplusplus
}
#endif

#endif
