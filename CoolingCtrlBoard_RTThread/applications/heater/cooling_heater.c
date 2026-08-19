/*
 * CoolingCtrlBoard 加热输出模块实现
 *
 * 唯一拥有加热功率输出，GPIO 写入全部通过 cooling_board_port.c。
 * 安全策略全部由统一安全模块裁决：false 请求永远允许并立即写低，
 * true 请求在持锁后与写开后复查安全条件，
 * 任何条件变化都立即拉低并拒绝本次请求。
 *
 * 本模块不创建线程，无周期任务；只依赖板级适配与统一安全模块。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_heater.h"

#include "hardware/cooling_board_port.h"
#include "safety/cooling_safety.h"

static struct rt_mutex heater_lock;
static cooling_heater_snapshot_t heater_snapshot;

void cooling_heater_start(void)
{
    /*
     * 启动必须先硬件拉低再建立状态：板级适配层保证先写低、
     * 再配置输出模式，任何失败路径都保持加热功率级关闭。
     */
    cooling_board_heater_init();

    rt_mutex_init(&heater_lock, "htrlock", RT_IPC_FLAG_PRIO);

    /* 建立初始状态：加热关闭。 */
    rt_memset(&heater_snapshot, 0, sizeof(heater_snapshot));
    heater_snapshot.tick = rt_tick_get();
}

rt_err_t cooling_heater_set(rt_bool_t enabled)
{
    rt_err_t result = RT_EOK;
    rt_bool_t effective = RT_FALSE;

    /*
     * false 请求永远允许：先不等待互斥量立即写低，缩短关断延迟；
     * 获得互斥量后还要再写低一次，防止并发的开启请求在第一次
     * 写低之后又把引脚写高。
     */
    if (!enabled)
    {
        cooling_board_heater_write(RT_FALSE);

        rt_mutex_take(&heater_lock, RT_WAITING_FOREVER);
        /* 与开启路径串行后再次关断，保证函数返回时硬件为低。 */
        cooling_board_heater_write(RT_FALSE);
        heater_snapshot.heater_on = RT_FALSE;
        heater_snapshot.sequence++;
        heater_snapshot.tick = rt_tick_get();
        rt_mutex_release(&heater_lock);
        return RT_EOK;
    }

    rt_mutex_take(&heater_lock, RT_WAITING_FOREVER);

    /* 持锁后复查安全条件，防止检查后被抢占的竞态窗口。 */
    if (!cooling_safety_heater_allowed())
    {
        cooling_board_heater_write(RT_FALSE);
        result = -RT_EBUSY;
    }
    else
    {
        cooling_board_heater_write(RT_TRUE);
        effective = RT_TRUE;

        /*
         * 写开后再次复查：期间安全条件已变化则立即拉低，
         * 保证任何关闭请求/状态变化都不会被旧快照重新覆盖。
         */
        if (!cooling_safety_heater_allowed())
        {
            cooling_board_heater_write(RT_FALSE);
            effective = RT_FALSE;
            result = -RT_EBUSY;
        }
    }

    heater_snapshot.heater_on = effective;
    heater_snapshot.sequence++;
    heater_snapshot.tick = rt_tick_get();
    rt_mutex_release(&heater_lock);

    return result;
}

void cooling_heater_get_snapshot(cooling_heater_snapshot_t *snapshot)
{
    rt_mutex_take(&heater_lock, RT_WAITING_FOREVER);
    *snapshot = heater_snapshot;
    rt_mutex_release(&heater_lock);
}

#ifdef RT_USING_FINSH
/* 调试命令：在 MSH 中输入 heater_status 查看加热输出状态。 */
static void heater_status(int argc, char **argv)
{
    cooling_heater_snapshot_t snapshot;

    (void)argc;
    (void)argv;

    cooling_heater_get_snapshot(&snapshot);

    rt_kprintf("heater seq=%u tick=%u output=%s\n",
               (unsigned int)snapshot.sequence,
               (unsigned int)snapshot.tick,
               snapshot.heater_on ? "on" : "off");
}
MSH_CMD_EXPORT(heater_status, show heater output state (read only));
#endif
