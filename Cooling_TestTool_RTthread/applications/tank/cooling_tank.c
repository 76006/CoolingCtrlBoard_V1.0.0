/*
 * CoolingCtrlBoard 罐体（LS 限位开关）输入模块实现
 *
 * 本模块只负责消抖状态机和线程安全的数据发布，GPIO 读取通过
 * cooling_board_port.c 完成。稳定后的罐体状态发布到统一安全模块；
 * 罐体由存在变为不存在时，在释放本模块互斥量并更新安全状态之后
 * 立即调用 cooling_heater_set(false)，绝不持有 tank_lock 调用加热
 * 模块，避免跨模块锁序问题。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_tank.h"

#include "hardware/cooling_board_port.h"
#include "cooling_config.h"
#include "heater/cooling_heater.h"
#include "safety/cooling_safety.h"

static struct rt_mutex tank_lock;
static cooling_tank_snapshot_t tank_snapshot;

#ifdef RT_USING_FINSH
static volatile rt_bool_t tank_print_enabled = RT_FALSE;
static void tank_print_snapshot(const cooling_tank_snapshot_t *snapshot);
#endif

/*
 * 连续 N 次采到相同电平后才确认新状态。
 * 这里保留“电平”而不是直接解释为“触发/未触发”，因为限位开关
 * 的常开/常闭定义由原理图语义决定：稳定低电平表示已插罐。
 */
static void tank_monitor_entry(void *parameter)
{
    rt_uint8_t candidate_level = 0U;
    rt_uint8_t candidate_count = 0U;
    rt_bool_t previous_present = RT_FALSE;

#ifdef RT_USING_FINSH
    rt_tick_t last_print_tick = rt_tick_get();
#endif

    (void)parameter;

    while (1)
    {
        rt_uint8_t raw_level = cooling_board_tank_read();
        rt_bool_t first_stable = RT_FALSE;
        rt_bool_t state_changed = RT_FALSE;
        rt_bool_t present = RT_FALSE;

        if (raw_level == candidate_level)
        {
            if (candidate_count < COOLING_TANK_DEBOUNCE_SAMPLES)
            {
                candidate_count++;
            }
        }
        else
        {
            candidate_level = raw_level;
            candidate_count = 1U;
        }

        rt_mutex_take(&tank_lock, RT_WAITING_FOREVER);
        tank_snapshot.raw_level = raw_level;

        if (candidate_count >= COOLING_TANK_DEBOUNCE_SAMPLES)
        {
            if (!tank_snapshot.valid)
            {
                /* 首次达到稳定条件，只建立初值，不计为一次变化。 */
                tank_snapshot.stable_level = candidate_level;
                tank_snapshot.valid = RT_TRUE;
                first_stable = RT_TRUE;
            }
            else if (tank_snapshot.stable_level != candidate_level)
            {
                tank_snapshot.stable_level = candidate_level;
                tank_snapshot.change_count++;
                state_changed = RT_TRUE;
            }
        }

        /* LS 为 10k 上拉：稳定低电平表示已插罐。 */
        present = tank_snapshot.valid &&
                  (tank_snapshot.stable_level == 0U);
        tank_snapshot.tank_present = present;
        tank_snapshot.sequence++;
        tank_snapshot.tick = rt_tick_get();
        rt_mutex_release(&tank_lock);

        if (first_stable || state_changed)
        {
            /* 先向安全模块发布罐体状态；此处不持有 tank_lock。 */
            cooling_safety_set_tank(present);

            /*
             * 存在 -> 不存在：更新安全状态后立即关闭加热输出，
             * 同样不持有 tank_lock，避免锁序反转。
             */
            if (previous_present && !present)
            {
                (void)cooling_heater_set(RT_FALSE);
            }
            previous_present = present;
        }

#ifdef RT_USING_FINSH
        if (!tank_print_enabled)
        {
            last_print_tick = rt_tick_get();
        }
        else if ((rt_tick_get() - last_print_tick) >=
                 rt_tick_from_millisecond(COOLING_STATUS_PRINT_PERIOD_MS))
        {
            cooling_tank_snapshot_t snapshot;

            cooling_tank_get_snapshot(&snapshot);
            tank_print_snapshot(&snapshot);
            last_print_tick = snapshot.tick;
        }
#endif

        rt_thread_mdelay(COOLING_TANK_SAMPLE_PERIOD_MS);
    }
}

rt_err_t cooling_tank_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    /* 板级适配层负责 LS 引脚输入模式配置。 */
    cooling_board_tank_input_init();

    rt_mutex_init(&tank_lock, "tanklock", RT_IPC_FLAG_PRIO);
    rt_memset(&tank_snapshot, 0, sizeof(tank_snapshot));
    tank_snapshot.raw_level = cooling_board_tank_read();
    tank_snapshot.tick = rt_tick_get();

    thread = rt_thread_create("tankmon", tank_monitor_entry, RT_NULL,
                              COOLING_TANK_THREAD_STACK_SIZE,
                              COOLING_TANK_THREAD_PRIORITY,
                              COOLING_TANK_THREAD_TIMESLICE);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(thread);
        return result;
    }

    return RT_EOK;
}

void cooling_tank_get_snapshot(cooling_tank_snapshot_t *snapshot)
{
    rt_mutex_take(&tank_lock, RT_WAITING_FOREVER);
    *snapshot = tank_snapshot;
    rt_mutex_release(&tank_lock);
}

#ifdef RT_USING_FINSH
static void tank_print_snapshot(const cooling_tank_snapshot_t *snapshot)
{
    rt_kprintf("tank seq=%u tick=%u raw=%u stable=%u valid=%u "
               "present=%s changes=%u\n",
               (unsigned int)snapshot->sequence,
               (unsigned int)snapshot->tick,
               (unsigned int)snapshot->raw_level,
               (unsigned int)snapshot->stable_level,
               (unsigned int)snapshot->valid,
               snapshot->tank_present ? "yes" : "no",
               (unsigned int)snapshot->change_count);
}

/* 每次执行 tank_status，反转罐体状态的循环打印。 */
static void tank_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tank_print_enabled = tank_print_enabled ? RT_FALSE : RT_TRUE;
    rt_kprintf("tank print %s\n", tank_print_enabled ? "on" : "off");
}
MSH_CMD_EXPORT(tank_status, toggle continuous tank status printing);
#endif
