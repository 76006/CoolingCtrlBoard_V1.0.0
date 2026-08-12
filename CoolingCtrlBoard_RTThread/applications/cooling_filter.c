/*
 * CoolingCtrlBoard ADC 滤波算法实现
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_filter.h"

#include <stddef.h>

/*
 * 对当前窗口进行插入排序并取中值。
 * 窗口很小（默认 5 点），插入排序代码小、无动态内存，适合 MCU。
 */
static uint16_t filter_median(const cooling_adc_filter_t *filter)
{
    uint16_t sorted[COOLING_ADC_FILTER_WINDOW];
    uint8_t i;
    uint8_t j;

    for (i = 0; i < filter->count; i++)
    {
        sorted[i] = filter->sample[i];
    }

    for (i = 1; i < filter->count; i++)
    {
        uint16_t value = sorted[i];

        j = i;
        while ((j > 0U) && (sorted[j - 1U] > value))
        {
            sorted[j] = sorted[j - 1U];
            j--;
        }
        sorted[j] = value;
    }

    return sorted[filter->count / 2U];
}

void cooling_adc_filter_init(cooling_adc_filter_t *filter)
{
    uint8_t i;

    if (filter == NULL)
    {
        return;
    }

    for (i = 0; i < COOLING_ADC_FILTER_WINDOW; i++)
    {
        filter->sample[i] = 0U;
    }
    filter->count = 0U;
    filter->next = 0U;
    filter->iir_state = 0;
    filter->iir_initialized = false;
}

uint16_t cooling_adc_filter_update(cooling_adc_filter_t *filter, uint16_t raw)
{
    int32_t target;

    if (filter == NULL)
    {
        return raw;
    }

    /* 环形保存最近 N 次采样。 */
    filter->sample[filter->next] = raw;
    filter->next = (uint8_t)((filter->next + 1U) % COOLING_ADC_FILTER_WINDOW);
    if (filter->count < COOLING_ADC_FILTER_WINDOW)
    {
        filter->count++;
    }

    /*
     * 中值滤波先去掉单点毛刺，再用定点 IIR 平滑。
     * 使用定点数可避免在 Cortex-M4 上引入不必要的浮点运算和库依赖。
     */
    target = (int32_t)filter_median(filter) * COOLING_ADC_IIR_SCALE;
    if (!filter->iir_initialized)
    {
        filter->iir_state = target;
        filter->iir_initialized = true;
    }
    else
    {
        filter->iir_state +=
            (target - filter->iir_state) / COOLING_ADC_IIR_DIVISOR;
    }

    return (uint16_t)((filter->iir_state +
                       (COOLING_ADC_IIR_SCALE / 2)) /
                      COOLING_ADC_IIR_SCALE);
}
