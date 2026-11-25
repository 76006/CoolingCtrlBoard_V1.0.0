#ifndef COOLING_RELIEF_SERVICE_H
#define COOLING_RELIEF_SERVICE_H

#include <rtthread.h>
#include "adc/cooling_adc.h"
#include "safety/cooling_safety.h"

rt_err_t relief_service_init(void);
rt_bool_t relief_service_control(const cooling_adc_snapshot_t *adc,
                                 rt_uint32_t blocks, rt_int16_t ntc1,
                                 rt_uint16_t *target);

#endif
