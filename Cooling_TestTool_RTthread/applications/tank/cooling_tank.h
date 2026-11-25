/*
 * CoolingCtrlBoard 罐体（LS 限位开关）输入模块公共接口
 *
 * 只负责 LS 输入采样、消抖和罐体存在语义，不包含任何加热控制。
 * 原理图语义：稳定低电平表示已插罐。每次首次稳定或状态变化后
 * 把罐体状态发布到统一安全模块；罐体由存在变为不存在时立即
 * 关闭加热输出（通过加热模块，不直接操作 GPIO）。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_TANK_H
#define APPLICATIONS_COOLING_TANK_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_uint32_t sequence;        /* 状态快照序号 */
    rt_uint32_t change_count;    /* 消抖后确认的状态变化次数 */
    rt_tick_t tick;              /* 最近更新时间 */
    rt_uint8_t raw_level;        /* PB15 当前原始电平 */
    rt_uint8_t stable_level;     /* 消抖后的稳定电平 */
    rt_bool_t valid;             /* 稳定电平是否已经有效 */
    rt_bool_t tank_present;      /* 稳定低电平 = 已插罐 */
} cooling_tank_snapshot_t;

/* 启动 LS 监测任务。 */
rt_err_t cooling_tank_start(void);

/* 复制一份线程安全的罐体快照；不能在中断中调用。 */
void cooling_tank_get_snapshot(cooling_tank_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_TANK_H */
