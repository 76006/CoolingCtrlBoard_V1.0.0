/*
 * CoolingCtrlBoard 开关输入与加热输出服务公共接口
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_SWITCH_H
#define APPLICATIONS_COOLING_SWITCH_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_uint32_t sequence;            /* 状态快照序号 */
    rt_uint32_t limit_change_count;  /* 消抖后确认的变化次数 */
    rt_tick_t tick;                  /* 最近更新时间 */
    rt_uint8_t raw_limit_level;      /* PB15 当前原始电平 */
    rt_uint8_t limit_level;          /* 消抖后的稳定电平 */
    rt_bool_t limit_valid;           /* 稳定电平是否已经有效 */
    rt_bool_t heater_on;             /* 当前加热输出命令状态 */
} cooling_switch_snapshot_t;

/* 启动 PB15 监测任务，并确保 PB0 默认关闭。 */
rt_err_t cooling_switch_start(void);

/* 供 CAN 等线程读取一份完整状态；不能在中断中调用。 */
rt_err_t cooling_switch_get_snapshot(cooling_switch_snapshot_t *snapshot);

/*
 * 显式设置加热输出。本服务不会自动开启加热；调用者必须先完成
 * 温度、限位开关、通信超时等安全联锁判断。
 */
rt_err_t cooling_heater_set(rt_bool_t enabled);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_SWITCH_H */
