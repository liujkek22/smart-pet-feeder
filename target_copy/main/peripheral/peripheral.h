#ifndef PERIPHERAL_H
#define PERIPHERAL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "driver/gpio.h"
#include "stdint.h"

    void hx711_init(void);
    int hx711_get_weight(void);
    void warm_init(void);
    void warm_on(void);
    void warm_off(void);
#ifdef __cplusplus
}
#endif

#endif