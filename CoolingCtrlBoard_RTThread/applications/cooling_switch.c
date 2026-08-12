/*
 * CoolingCtrlBoard 开关输入与加热输出服务
 *
 * 本层只负责消抖状态机和线程安全的数据发布，GPIO 操作全部通过
 * cooling_board_port.c 完成，便于更换引脚、MCU 或底层驱动。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_switch.h"

#include "cooling_board_port.h"
#include "cooling_config.h"

static rt_thread_t switch_thread;
static rt_mutex_t switch_lock;
static cooling_switch_snapshot_t switch_snapshot;

/*
 * 连续 N 次采到相同电平后才确认新状态。
 * 这里保留“电平”而不是直接解释为“触发/未触发”，因为限位开关
 * 的常开/常闭定义应由更上层的安全策略决定。
 */
static void switch_monitor_entry(void *parameter)
{
    rt_uint8_t candidate_level = 0U;
    rt_uint8_t candidate_count = 0U;

    (void)parameter;

    while (1)
    {
        rt_uint8_t raw_level = cooling_board_limit_read();

        if (raw_level == candidate_level)
        {
            if (candidate_count < COOLING_SWITCH_DEBOUNCE_SAMPLES)
            {
                candidate_count++;
            }
        }
        else
        {
            candidate_level = raw_level;
            candidate_count = 1U;
        }

        if (rt_mutex_take(switch_lock, RT_WAITING_FOREVER) == RT_EOK)
        {
            switch_snapshot.raw_limit_level = raw_level;

            if (candidate_count >= COOLING_SWITCH_DEBOUNCE_SAMPLES)
            {
                if (!switch_snapshot.limit_valid)
                {
                    /* 首次达到稳定条件，只建立初值，不计为一次变化。 */
                    switch_snapshot.limit_level = candidate_level;
                    switch_snapshot.limit_valid = RT_TRUE;
                }
                else if (switch_snapshot.limit_level != candidate_level)
                {
                    switch_snapshot.limit_level = candidate_level;
                    switch_snapshot.limit_change_count++;
                }
            }

            switch_snapshot.sequence++;
            switch_snapshot.tick = rt_tick_get();
            rt_mutex_release(switch_lock);
        }

        rt_thread_mdelay(COOLING_SWITCH_SAMPLE_PERIOD_MS);
    }
}

rt_err_t cooling_switch_start(void)
{
    rt_err_t result;

    if (switch_thread != RT_NULL)
    {
        return RT_EOK;
    }

    /* 板级适配层负责引脚设置，并先将加热输出置为关闭。 */
    cooling_board_io_init();

    switch_lock = rt_mutex_create("swlock", RT_IPC_FLAG_PRIO);
    if (switch_lock == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_memset(&switch_snapshot, 0, sizeof(switch_snapshot));
    switch_snapshot.raw_limit_level = cooling_board_limit_read();
    switch_snapshot.tick = rt_tick_get();

    switch_thread = rt_thread_create("swmon",
                                     switch_monitor_entry,
                                     RT_NULL,
                                     COOLING_SWITCH_THREAD_STACK_SIZE,
                                     COOLING_SWITCH_THREAD_PRIORITY,
                                     COOLING_SWITCH_THREAD_TIMESLICE);
    if (switch_thread == RT_NULL)
    {
        rt_mutex_delete(switch_lock);
        switch_lock = RT_NULL;
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(switch_thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(switch_thread);
        switch_thread = RT_NULL;
        rt_mutex_delete(switch_lock);
        switch_lock = RT_NULL;
        return result;
    }

    return RT_EOK;
}

rt_err_t cooling_switch_get_snapshot(cooling_switch_snapshot_t *snapshot)
{
    rt_err_t result;

    if (snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (switch_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(switch_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        *snapshot = switch_snapshot;
        rt_mutex_release(switch_lock);
    }

    return result;
}

rt_err_t cooling_heater_set(rt_bool_t enabled)
{
    rt_err_t result;

    if (switch_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(switch_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        /*
         * 此函数只执行明确的输出命令，不在这里隐含温控策略。
         * 温度、限位、CAN 超时等联锁必须由上层控制状态机先判断。
         */
        cooling_board_heater_write(enabled);
        switch_snapshot.heater_on = enabled ? RT_TRUE : RT_FALSE;
        switch_snapshot.sequence++;
        switch_snapshot.tick = rt_tick_get();
        rt_mutex_release(switch_lock);
    }

    return result;
}

#ifdef RT_USING_FINSH
/* 调试命令：在 MSH 中输入 io_values 查看开关与加热输出状态。 */
static void io_values(int argc, char **argv)
{
    cooling_switch_snapshot_t snapshot;

    (void)argc;
    (void)argv;

    if (cooling_switch_get_snapshot(&snapshot) != RT_EOK)
    {
        rt_kprintf("switch monitor is not ready\n");
        return;
    }

    rt_kprintf("io seq=%u tick=%u raw_limit=%u limit=%u valid=%u changes=%u heater=%s\n",
               (unsigned int)snapshot.sequence,
               (unsigned int)snapshot.tick,
               (unsigned int)snapshot.raw_limit_level,
               (unsigned int)snapshot.limit_level,
               (unsigned int)snapshot.limit_valid,
               (unsigned int)snapshot.limit_change_count,
               snapshot.heater_on ? "on" : "off");
}
MSH_CMD_EXPORT(io_values, show debounced switch and heater state);
#endif
