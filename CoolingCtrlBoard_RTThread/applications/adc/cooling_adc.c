/*
 * CoolingCtrlBoard ADC 采样服务
 *
 * 本层负责采样节拍、私有滤波算法和数据发布，不直接访问 ADC 驱动。
 * 硬件访问统一由 cooling_board_port.c 完成。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_adc.h"

#include "hardware/cooling_board_port.h"
#include "cooling_config.h"

typedef struct
{
    rt_uint16_t sample[COOLING_ADC_FILTER_WINDOW];
    rt_uint8_t count;
    rt_uint8_t next;
    rt_int32_t iir_state;
    rt_bool_t iir_initialized;
} cooling_adc_filter_t;

/* 滤波器仅由 ADC 采样线程使用，不向其他模块暴露状态和算法接口。 */
static rt_uint16_t adc_filter_median(const cooling_adc_filter_t *filter)
{
    rt_uint16_t sorted[COOLING_ADC_FILTER_WINDOW];
    rt_uint8_t i;
    rt_uint8_t j;

    for (i = 0U; i < filter->count; i++)
    {
        sorted[i] = filter->sample[i];
    }

    for (i = 1U; i < filter->count; i++)
    {
        rt_uint16_t value = sorted[i];

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

static void adc_filter_init(cooling_adc_filter_t *filter)
{
    rt_uint8_t i;

    for (i = 0U; i < COOLING_ADC_FILTER_WINDOW; i++)
    {
        filter->sample[i] = 0U;
    }
    filter->count = 0U;
    filter->next = 0U;
    filter->iir_state = 0;
    filter->iir_initialized = RT_FALSE;
}

static rt_uint16_t adc_filter_update(cooling_adc_filter_t *filter,
                                     rt_uint16_t raw)
{
    rt_int32_t target;

    filter->sample[filter->next] = raw;
    filter->next = (rt_uint8_t)((filter->next + 1U) %
                                COOLING_ADC_FILTER_WINDOW);
    if (filter->count < COOLING_ADC_FILTER_WINDOW)
    {
        filter->count++;
    }

    target = (rt_int32_t)adc_filter_median(filter) * COOLING_ADC_IIR_SCALE;
    if (!filter->iir_initialized)
    {
        filter->iir_state = target;
        filter->iir_initialized = RT_TRUE;
    }
    else
    {
        filter->iir_state +=
            (target - filter->iir_state) / COOLING_ADC_IIR_DIVISOR;
    }

    return (rt_uint16_t)((filter->iir_state +
                          (COOLING_ADC_IIR_SCALE / 2)) /
                         COOLING_ADC_IIR_SCALE);
}

static const char *const adc_input_name[COOLING_ADC_CHANNEL_COUNT] =
{
    "ntc0",
    "ntc1",
    "hysn2"
};

static struct rt_mutex adc_snapshot_lock;
static cooling_adc_snapshot_t adc_snapshot;

/*
 * ADC 任务是硬件 ADC 的唯一使用者。
 * 每轮完成三路采样后再整体发布快照，CAN 线程不会读到半更新的数据。
 */
static void adc_sample_entry(void *parameter)
{
    cooling_adc_filter_t filter[COOLING_ADC_CHANNEL_COUNT];
    cooling_adc_snapshot_t next_snapshot = {0};
    rt_uint8_t previous_valid_mask = 0xFFU;
    rt_uint8_t input;

    (void)parameter;

    for (input = 0U; input < COOLING_ADC_CHANNEL_COUNT; input++)
    {
        adc_filter_init(&filter[input]);
    }

    while (1)
    {
        next_snapshot.valid_mask = 0U;

        for (input = 0U; input < COOLING_ADC_CHANNEL_COUNT; input++)
        {
            rt_uint16_t raw;

            if (cooling_board_adc_read((cooling_adc_input_t)input, &raw) == RT_EOK)
            {
                rt_uint16_t filtered =
                    adc_filter_update(&filter[input], raw);

                next_snapshot.value[input].raw = raw;
                next_snapshot.value[input].filtered = filtered;
                next_snapshot.value[input].millivolts =
                    (rt_uint16_t)(((rt_uint32_t)filtered *
                                   COOLING_ADC_REFERENCE_MV +
                                   (COOLING_ADC_FULL_SCALE / 2U)) /
                                  COOLING_ADC_FULL_SCALE);
                next_snapshot.valid_mask |= COOLING_ADC_VALID_BIT(input);
            }
        }

        next_snapshot.sequence++;
        next_snapshot.tick = rt_tick_get();

        /* 互斥量只保护一次结构体复制，锁定时间很短。 */
        rt_mutex_take(&adc_snapshot_lock, RT_WAITING_FOREVER);
        adc_snapshot = next_snapshot;
        rt_mutex_release(&adc_snapshot_lock);

        /* 有效状态发生变化时才打印，避免硬件故障时刷屏。 */
        if ((next_snapshot.valid_mask != previous_valid_mask) &&
            (next_snapshot.valid_mask != COOLING_ADC_VALID_ALL))
        {
            rt_kprintf("adc sampler: valid mask 0x%02x\n",
                       (unsigned int)next_snapshot.valid_mask);
        }
        previous_valid_mask = next_snapshot.valid_mask;

        rt_thread_mdelay(COOLING_ADC_SAMPLE_PERIOD_MS);
    }
}

rt_err_t cooling_adc_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    rt_mutex_init(&adc_snapshot_lock, "adclock", RT_IPC_FLAG_PRIO);
    rt_memset(&adc_snapshot, 0, sizeof(adc_snapshot));

    result = cooling_board_adc_init();
    if (result != RT_EOK)
    {
        return result;
    }

    thread = rt_thread_create("adcflt", adc_sample_entry, RT_NULL,
                              COOLING_ADC_THREAD_STACK_SIZE,
                              COOLING_ADC_THREAD_PRIORITY,
                              COOLING_ADC_THREAD_TIMESLICE);
    if (thread == RT_NULL)
    {
        cooling_board_adc_deinit();
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(thread);
        cooling_board_adc_deinit();
        return result;
    }

    return RT_EOK;
}

void cooling_adc_get_snapshot(cooling_adc_snapshot_t *snapshot)
{
    rt_mutex_take(&adc_snapshot_lock, RT_WAITING_FOREVER);
    *snapshot = adc_snapshot;
    rt_mutex_release(&adc_snapshot_lock);
}

/* 线性换算：value = (adc * scale) / 4095 + offset。 */
#if (COOLING_PRESSURE_CALIBRATED != 0) || \
    (COOLING_TEMPERATURE_CALIBRATED != 0)
static uint32_t adc_linear_value(uint16_t filtered_adc,
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
    *pressure_mpa1000 = 0U;
#if COOLING_PRESSURE_CALIBRATED != 0
    *pressure_mpa1000 = (uint16_t)adc_linear_value(
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

    *temperature_deg10 = 0U;
    cooling_adc_get_snapshot(&adc);
    if ((adc.valid_mask & COOLING_ADC_VALID_BIT(channel)) == 0U)
    {
        return -1;
    }

#if COOLING_TEMPERATURE_CALIBRATED != 0
    *temperature_deg10 = (uint16_t)adc_linear_value(
        adc.value[channel].filtered,
        COOLING_TEMPERATURE_ADC_FULL_DEG10,
        COOLING_TEMPERATURE_ADC_OFFSET_DEG10);
    return 0;
#else
    return -1;
#endif
}

#ifdef RT_USING_FINSH
/* 调试命令：在 MSH 中输入 adc_values 查看三路采样值。 */
static void adc_values(int argc, char **argv)
{
    cooling_adc_snapshot_t snapshot;
    rt_uint8_t input;

    (void)argc;
    (void)argv;

    cooling_adc_get_snapshot(&snapshot);

    rt_kprintf("adc seq=%u tick=%u valid=0x%02x\n",
               (unsigned int)snapshot.sequence,
               (unsigned int)snapshot.tick,
               (unsigned int)snapshot.valid_mask);
    for (input = 0U; input < COOLING_ADC_CHANNEL_COUNT; input++)
    {
        rt_kprintf("  %s: raw=%u filtered=%u mv=%u %s\n",
                   adc_input_name[input],
                   (unsigned int)snapshot.value[input].raw,
                   (unsigned int)snapshot.value[input].filtered,
                   (unsigned int)snapshot.value[input].millivolts,
                   (snapshot.valid_mask & COOLING_ADC_VALID_BIT(input)) ?
                       "valid" : "invalid");
    }
}
MSH_CMD_EXPORT(adc_values, show filtered CoolingCtrlBoard ADC values);
#endif
