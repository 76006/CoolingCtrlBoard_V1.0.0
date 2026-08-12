/*
 * CoolingCtrlBoard ADC 采样服务
 *
 * 本层负责采样节拍、滤波器编排和数据发布，不直接访问 ADC 驱动。
 * 硬件访问由 cooling_board_port.c 完成，滤波由 cooling_filter.c 完成。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_adc.h"

#include "cooling_board_port.h"
#include "cooling_config.h"
#include "cooling_filter.h"

static const char *const adc_input_name[COOLING_ADC_CHANNEL_COUNT] =
{
    "ntc0",
    "ntc1",
    "hysn2"
};

static rt_thread_t adc_thread;
static rt_mutex_t adc_snapshot_lock;
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
        cooling_adc_filter_init(&filter[input]);
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
                    (rt_uint16_t)cooling_adc_filter_update(&filter[input], raw);

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
        if (rt_mutex_take(adc_snapshot_lock, RT_WAITING_FOREVER) == RT_EOK)
        {
            adc_snapshot = next_snapshot;
            rt_mutex_release(adc_snapshot_lock);
        }

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
    rt_err_t result;

    /* 重复调用时直接返回，防止创建多个任务同时访问 ADC。 */
    if (adc_thread != RT_NULL)
    {
        return RT_EOK;
    }

    result = cooling_board_adc_init();
    if (result != RT_EOK)
    {
        return result;
    }

    adc_snapshot_lock = rt_mutex_create("adclock", RT_IPC_FLAG_PRIO);
    if (adc_snapshot_lock == RT_NULL)
    {
        cooling_board_adc_deinit();
        return -RT_ENOMEM;
    }

    rt_memset(&adc_snapshot, 0, sizeof(adc_snapshot));
    adc_thread = rt_thread_create("adcflt",
                                  adc_sample_entry,
                                  RT_NULL,
                                  COOLING_ADC_THREAD_STACK_SIZE,
                                  COOLING_ADC_THREAD_PRIORITY,
                                  COOLING_ADC_THREAD_TIMESLICE);
    if (adc_thread == RT_NULL)
    {
        rt_mutex_delete(adc_snapshot_lock);
        adc_snapshot_lock = RT_NULL;
        cooling_board_adc_deinit();
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(adc_thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(adc_thread);
        adc_thread = RT_NULL;
        rt_mutex_delete(adc_snapshot_lock);
        adc_snapshot_lock = RT_NULL;
        cooling_board_adc_deinit();
        return result;
    }

    return RT_EOK;
}

rt_err_t cooling_adc_get_snapshot(cooling_adc_snapshot_t *snapshot)
{
    rt_err_t result;

    if (snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (adc_snapshot_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(adc_snapshot_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        *snapshot = adc_snapshot;
        rt_mutex_release(adc_snapshot_lock);
    }

    return result;
}

#ifdef RT_USING_FINSH
/* 调试命令：在 MSH 中输入 adc_values 查看三路采样值。 */
static void adc_values(int argc, char **argv)
{
    cooling_adc_snapshot_t snapshot;
    rt_uint8_t input;

    (void)argc;
    (void)argv;

    if (cooling_adc_get_snapshot(&snapshot) != RT_EOK)
    {
        rt_kprintf("adc sampler is not ready\n");
        return;
    }

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
