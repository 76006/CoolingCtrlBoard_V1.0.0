/*
 * CoolingCtrlBoard ADC 采样、滤波与传感器换算公共接口
 *
 * 负责发布滤波后的 ADC 数据，并提供压力、温度定点值换算。
 * 上层不需要了解 ADC 驱动、引脚映射和滤波器内部实现。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_ADC_H
#define APPLICATIONS_COOLING_ADC_H

#include <stdint.h>
#include <rtthread.h>

#include "cooling_adc_channels.h"

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
    rt_uint8_t heater_on;     /* 本组采样完成时的加热输出命令 */
    cooling_adc_value_t value[COOLING_ADC_CHANNEL_COUNT];
} cooling_adc_snapshot_t;

/* 启动周期采样服务。 */
rt_err_t cooling_adc_start(void);

/*
 * 为其他线程复制一份完整、一致的数据快照，例如供 CAN 发送任务使用。
 * 使用某一路之前必须检查 valid_mask。本函数内部使用互斥量，不能在中断中调用。
 */
void cooling_adc_get_snapshot(cooling_adc_snapshot_t *snapshot);

/* 根据调理电路增益还原压力传感器输出电压（mV）。 */
uint16_t cooling_pressure_sensor_mv(uint16_t adc);
/* 将量程内的压力（0.001 MPa）反算为控制阈值的 ADC 码。 */
uint16_t cooling_pressure_adc_from_mpa1000(uint16_t pressure_mpa1000);
/* 滤波 ADC 值换算为 0.001 MPa；未标定时返回 -1。 */
int cooling_sensor_pressure_mpa1000(uint16_t filtered_adc,
                                    uint16_t *pressure_mpa1000);

/* Convert one channel from a caller-owned snapshot (no second ADC read). */
int cooling_ntc_from_adc(cooling_adc_input_t input, rt_uint16_t adc,
                         rt_int16_t *temperature_deg10);

/* 按 NTC1 厂家温阻表换算为有符号 0.1 摄氏度；供温度控制使用。 */
int cooling_sensor_ntc1_temperature_deg10(int16_t *temperature_deg10);

/* 按 NTC1 厂家温阻表换算为 0.1 摄氏度；供 CAN 温度字段使用。 */
int cooling_sensor_temperature_deg10(uint16_t *temperature_deg10);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_ADC_H */
