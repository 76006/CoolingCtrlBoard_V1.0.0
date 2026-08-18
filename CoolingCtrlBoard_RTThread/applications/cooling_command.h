/*
 * CoolingCtrlBoard 外部命令与电源状态模块
 *
 * 保存与 CAN 收发解耦的电源/自动控制锁存状态，供压力控制核心、
 * CAN 状态上报等任务查询。所有读写都经过互斥量保护，任何线程
 * （包括 CAN 接收线程）都可以安全调用；不能在中断中调用。
 *
 * 默认状态：未上电（power_on = false）、自动加热控制启用
 * （auto_control = true）。收到主控上电广播前，加热联锁保持关闭。
 *
 * 本模块每次成功更新状态后会把 power_on/auto_control 发布到统一
 * 安全模块（safety/cooling_safety.h），自己不直接操作加热输出。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_COMMAND_H
#define APPLICATIONS_COOLING_COMMAND_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_bool_t power_on;      /* 上电锁存：主控 0x1000 广播上电后为真 */
    rt_bool_t auto_control;  /* 自动加热控制：主控 0x2010 帧控制 */
} cooling_command_state_t;

/* 初始化状态模块；重复调用幂等，默认状态见文件头注释。 */
rt_err_t cooling_command_init(void);

/* 复制一份线程安全的状态快照；不能在中断中调用。 */
rt_err_t cooling_command_get(cooling_command_state_t *state);

/* 设置上/下电锁存。本模块只保存状态，不直接操作加热输出。 */
rt_err_t cooling_command_set_power(rt_bool_t power_on);

/* 设置自动加热控制开关。本模块只保存状态，不直接操作加热输出。 */
rt_err_t cooling_command_set_auto_control(rt_bool_t enabled);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_COMMAND_H */
