/*
 * CoolingCtrlBoard 外部命令与电源状态模块实现
 *
 * 本模块只做状态保存与线程安全发布，不包含任何业务策略：
 * 加热输出是否允许开启由统一安全模块的联锁统一判定，本模块
 * 在成功更新自身状态后把电源/自动控制字段发布到安全模块。
 * 下电或禁用自动控制属于关断条件，发布后再通过加热模块公共接口
 * 主动关断；整个过程不持有 command_lock，避免跨模块锁序问题。
 * 安全模块不反向依赖本模块。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_command.h"

#include "heater/cooling_heater.h"
#include "safety/cooling_safety.h"

static rt_mutex_t command_lock;
static cooling_command_state_t command_state;

rt_err_t cooling_command_init(void)
{
    /* 重复调用幂等：互斥量只创建一次，默认状态只在首次建立。 */
    if (command_lock != RT_NULL)
    {
        return RT_EOK;
    }

    command_lock = rt_mutex_create("cmdlock", RT_IPC_FLAG_PRIO);
    if (command_lock == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    /* 默认状态：下电、自动加热控制启用。 */
    command_state.power_on = RT_FALSE;
    command_state.auto_control = RT_TRUE;

    /* 发布到统一安全模块；不持有 command_lock。 */
    (void)cooling_safety_set_power(RT_FALSE);
    (void)cooling_safety_set_auto_control(RT_TRUE);

    return RT_EOK;
}

rt_err_t cooling_command_get(cooling_command_state_t *state)
{
    rt_err_t result;

    if (state == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (command_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(command_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        *state = command_state;
        rt_mutex_release(command_lock);
    }

    return result;
}

rt_err_t cooling_command_set_power(rt_bool_t power_on)
{
    rt_err_t result;

    if (command_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(command_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        command_state.power_on = power_on ? RT_TRUE : RT_FALSE;
        rt_mutex_release(command_lock);
    }

    /* 状态更新成功后才发布安全模块；不持有 command_lock。 */
    if (result == RT_EOK)
    {
        (void)cooling_safety_set_power(power_on ? RT_TRUE : RT_FALSE);
        if (!power_on)
        {
            (void)cooling_heater_set(RT_FALSE);
        }
    }

    return result;
}

rt_err_t cooling_command_set_auto_control(rt_bool_t enabled)
{
    rt_err_t result;

    if (command_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(command_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        command_state.auto_control = enabled ? RT_TRUE : RT_FALSE;
        rt_mutex_release(command_lock);
    }

    /* 状态更新成功后才发布安全模块；不持有 command_lock。 */
    if (result == RT_EOK)
    {
        (void)cooling_safety_set_auto_control(enabled ? RT_TRUE : RT_FALSE);
        if (!enabled)
        {
            (void)cooling_heater_set(RT_FALSE);
        }
    }

    return result;
}
