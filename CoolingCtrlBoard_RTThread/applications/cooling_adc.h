/*
 * CoolingCtrlBoard ADC 采样服务公共接口
 *
 * CAN 等上层任务只需要包含本文件并读取数据快照，不需要了解
 * ADC 驱动、引脚映射和滤波器内部实现。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_ADC_H
#define APPLICATIONS_COOLING_ADC_H

#include <rtthread.h>

#include "cooling_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COOLING_ADC_VALID_BIT(input)    ((rt_uint8_t)(1U << (input)))
#define COOLING_ADC_VALID_ALL           ((rt_uint8_t)((1U << COOLING_ADC_CHANNEL_COUNT) - 1U))

typedef struct
{
    rt_uint16_t raw;          /* 本次硬件原始值，范围 0~4095 */
    rt_uint16_t filtered;     /* 中值 + IIR 滤波后的值 */
    rt_uint16_t millivolts;   /* 按配置的参考电压换算 */
} cooling_adc_value_t;

typedef struct
{
    rt_uint32_t sequence;     /* 每发布一组数据递增一次 */
    rt_tick_t tick;           /* 本组数据的 RT-Thread 时刻 */
    rt_uint8_t valid_mask;    /* 每一位对应一路 ADC 的有效状态 */
    cooling_adc_value_t value[COOLING_ADC_CHANNEL_COUNT];
} cooling_adc_snapshot_t;

/* 启动周期采样服务；重复调用不会重复创建线程。 */
rt_err_t cooling_adc_start(void);

/*
 * 为其他线程复制一份完整、一致的数据快照，例如供 CAN 发送任务使用。
 * 使用某一路之前必须检查 valid_mask。本函数内部使用互斥量，不能在中断中调用。
 */
rt_err_t cooling_adc_get_snapshot(cooling_adc_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_ADC_H */
