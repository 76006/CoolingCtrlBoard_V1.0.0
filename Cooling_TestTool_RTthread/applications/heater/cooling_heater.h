/*
 * CoolingCtrlBoard 加热输出模块公共接口
 *
 * 本模块是加热功率输出的唯一拥有者：关闭请求永远允许并立即执行；
 * 开启请求由统一安全模块裁决，本模块不直接知道主控命令、罐体、
 * OTA 或 UID 的任何细节。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_HEATER_H
#define APPLICATIONS_COOLING_HEATER_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_uint32_t sequence;    /* 快照序号，每次输出变化递增 */
    rt_tick_t tick;          /* 最近更新时间 */
    rt_bool_t heater_on;     /* 当前加热输出命令状态 */
} cooling_heater_snapshot_t;

/*
 * 启动加热模块：必须先硬件拉低再建立内部状态。
 */
void cooling_heater_start(void);

/*
 * 设置加热输出。false 请求永远允许并立即写低；
 * true 请求在写入前后都复查 cooling_safety_heater_allowed()，
 * 防止检查后被抢占的竞态。本模块不创建线程。
 */
rt_err_t cooling_heater_set(rt_bool_t enabled);

/* 复制一份线程安全的加热快照；不能在中断中调用。 */
void cooling_heater_get_snapshot(cooling_heater_snapshot_t *snapshot);

#ifdef RT_USING_FINSH
/* 由现有控制任务周期调用，不创建单独的调试线程。 */
void cooling_heater_debug_poll(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_HEATER_H */
