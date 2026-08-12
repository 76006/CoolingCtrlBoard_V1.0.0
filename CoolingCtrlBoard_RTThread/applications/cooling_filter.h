/*
 * CoolingCtrlBoard ADC 滤波算法接口
 *
 * 本层是纯 C 算法，不依赖 RT-Thread、STM32 HAL 或具体 ADC 驱动，
 * 可直接移植到其他 MCU/RTOS，也便于单独做算法测试。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_FILTER_H
#define APPLICATIONS_COOLING_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#include "cooling_config.h"

typedef struct
{
    uint16_t sample[COOLING_ADC_FILTER_WINDOW];
    uint8_t count;
    uint8_t next;
    int32_t iir_state;
    bool iir_initialized;
} cooling_adc_filter_t;

/* 清空一路滤波器的历史状态。 */
void cooling_adc_filter_init(cooling_adc_filter_t *filter);

/* 输入一个原始 ADC 值，返回“中值去毛刺 + IIR 平滑”后的结果。 */
uint16_t cooling_adc_filter_update(cooling_adc_filter_t *filter, uint16_t raw);

#endif /* APPLICATIONS_COOLING_FILTER_H */
