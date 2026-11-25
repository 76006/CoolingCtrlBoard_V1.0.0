/*
 * CoolingCtrlBoard 统一安全模块公共接口
 *
 * 集中管理“是否允许开启加热”的全部前置条件：授权（UID）、标定、
 * 上电、自动控制、罐体存在与 OTA 忙。各业务模块只通过 setter 发布
 * 自己负责的那一项状态，加热模块只查询 cooling_safety_heater_allowed()。
 *
 * 本模块不依赖 command、tank、heater、OTA、CAN、压力服务或 UID 等
 * 任何业务模块，只依赖 RT-Thread 与集中配置，从根上消除循环依赖。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_SAFETY_H
#define APPLICATIONS_COOLING_SAFETY_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统一安全快照：所有加热前置条件的一份线程安全拷贝。 */
typedef struct
{
    rt_uint32_t sequence;            /* 快照序号，每次更新递增 */
    rt_bool_t authorized;            /* UID 授权有效 */
    rt_bool_t calibrated;            /* 压力标定（取集中配置） */
    rt_bool_t power_on;              /* 主控上电锁存 */
    rt_bool_t auto_control_enabled;  /* 自动加热控制启用 */
    rt_bool_t tank_present;          /* 罐体已插入（LS 消抖稳定） */
    rt_bool_t ota_busy;              /* OTA 升级窗口（忙） */
    rt_uint32_t trip_sequence;       /* 记录两次控制采样间短暂出现的禁止条件 */
} cooling_safety_snapshot_t;

/*
 * 初始化安全模块。默认失败关闭：
 * authorized/power_on/tank_present/ota_busy 均为 false，
 * auto_control_enabled 为 true，calibrated 取 COOLING_PRESSURE_CALIBRATED。
 */
void cooling_safety_init(void);

/* 复制一份线程安全的安全快照；不能在中断中调用。 */
void cooling_safety_get_snapshot(cooling_safety_snapshot_t *snapshot);

/* 各状态生产者发布自己负责的字段；不能在中断中调用。 */
void cooling_safety_set_authorized(rt_bool_t authorized);
void cooling_safety_set_power(rt_bool_t power_on);
void cooling_safety_set_auto_control(rt_bool_t enabled);
void cooling_safety_set_tank(rt_bool_t tank_present);
void cooling_safety_set_ota(rt_bool_t busy);
/* Only the relief controller grants a run-specific permission to heat. */
void cooling_safety_set_test_heat(rt_bool_t enable, rt_uint32_t trip_sequence);

/*
 * 统一加热允许判定：authorized && calibrated && power_on &&
 * auto_control_enabled && tank_present && !ota_busy 时返回真；
 * 该快照由各状态生产者实时更新。
 */
rt_bool_t cooling_safety_heater_allowed(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_SAFETY_H */
