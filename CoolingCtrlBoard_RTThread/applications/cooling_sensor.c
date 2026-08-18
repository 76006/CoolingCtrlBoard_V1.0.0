/*
 * CoolingCtrlBoard 传感器物理量模块实现
 *
 * 压力接口换算调用方提供的滤波 ADC 值；温度接口自行读取 ADC 快照，
 * 并在本模块内部完成温度通道选择和有效位检查。物理量标定参数统一
 * 来自 cooling_sensor_config.h，通信模块不参与传感器计算。
 *
 * 当前标定开关为 0 时，接口明确返回无效并输出 0，避免把 ADC 原始值
 * 误当作真实压力或温度使用。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_sensor.h"

#include <stddef.h>

#include "cooling_adc.h"
#include "cooling_config.h"
#include "cooling_sensor_config.h"

/* 线性换算：value = (adc * scale) / 4095 + offset，并钳位到 16 位。 */
#if (COOLING_PRESSURE_CALIBRATED != 0) || \
    (COOLING_TEMPERATURE_CALIBRATED != 0)
static uint32_t sensor_linear(uint16_t filtered_adc,
                              uint32_t scale,
                              uint32_t offset)
{
    uint32_t value;

    value = ((uint32_t)filtered_adc * scale) / COOLING_ADC_FULL_SCALE;
    value += offset;
    if (value > UINT16_MAX)
    {
        value = UINT16_MAX;
    }

    return value;
}
#endif

int cooling_sensor_pressure_mpa1000(uint16_t filtered_adc,
                                    uint16_t *pressure_mpa1000)
{
    if (pressure_mpa1000 == NULL)
    {
        return -1;
    }

    *pressure_mpa1000 = 0U;
#if COOLING_PRESSURE_CALIBRATED != 0
    *pressure_mpa1000 = (uint16_t)sensor_linear(
        filtered_adc,
        COOLING_PRESSURE_ADC_FULL_MPA1000,
        COOLING_PRESSURE_ADC_OFFSET_MPA1000);
    return 0;
#else
    (void)filtered_adc;
    return -1;
#endif
}

int cooling_sensor_temperature_deg10(uint16_t *temperature_deg10)
{
    cooling_adc_snapshot_t adc;
    uint8_t channel = (uint8_t)COOLING_TEMPERATURE_ADC_INPUT;

    if (temperature_deg10 == NULL)
    {
        return -1;
    }

    *temperature_deg10 = 0U;
    if ((cooling_adc_get_snapshot(&adc) != RT_EOK) ||
        ((adc.valid_mask & COOLING_ADC_VALID_BIT(channel)) == 0U))
    {
        return -1;
    }

#if COOLING_TEMPERATURE_CALIBRATED != 0
    *temperature_deg10 = (uint16_t)sensor_linear(
        adc.value[channel].filtered,
        COOLING_TEMPERATURE_ADC_FULL_DEG10,
        COOLING_TEMPERATURE_ADC_OFFSET_DEG10);
    return 0;
#else
    return -1;
#endif
}
