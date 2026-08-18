/*
 * CoolingCtrlBoard 统一安全模块实现
 *
 * 本模块只保存状态并做线程安全发布，不含任何业务策略：各条件字段
 * 由生产者（命令、罐体、OTA、启动流程）通过 setter 发布，加热模块
 * 通过 cooling_safety_heater_allowed() 做唯一裁决。
 *
 * 默认失败关闭：未初始化或互斥量读取失败时，允许判定一律返回假。
 * 本文件不包含任何业务模块头文件，防止安全模块反向依赖业务层。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_safety.h"

#include "cooling_sensor_config.h"

static rt_mutex_t safety_lock;
static cooling_safety_snapshot_t safety_snapshot;

rt_err_t cooling_safety_init(void)
{
    /* 重复调用幂等：互斥量只创建一次，默认状态只在首次建立。 */
    if (safety_lock != RT_NULL)
    {
        return RT_EOK;
    }

    safety_lock = rt_mutex_create("safelock", RT_IPC_FLAG_PRIO);
    if (safety_lock == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    /* 默认失败关闭：authorized/power/tank/ota 全部为假。 */
    rt_memset(&safety_snapshot, 0, sizeof(safety_snapshot));
    safety_snapshot.auto_control_enabled = RT_TRUE;
    safety_snapshot.calibrated =
        (COOLING_PRESSURE_CALIBRATED != 0U) ? RT_TRUE : RT_FALSE;

    return RT_EOK;
}

rt_err_t cooling_safety_get_snapshot(cooling_safety_snapshot_t *snapshot)
{
    rt_err_t result;

    if (snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (safety_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(safety_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        *snapshot = safety_snapshot;
        rt_mutex_release(safety_lock);
    }

    return result;
}

rt_err_t cooling_safety_set_authorized(rt_bool_t authorized)
{
    rt_err_t result;

    if (safety_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(safety_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        safety_snapshot.authorized = authorized ? RT_TRUE : RT_FALSE;
        safety_snapshot.sequence++;
        rt_mutex_release(safety_lock);
    }

    return result;
}

rt_err_t cooling_safety_set_power(rt_bool_t power_on)
{
    rt_err_t result;

    if (safety_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(safety_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        safety_snapshot.power_on = power_on ? RT_TRUE : RT_FALSE;
        safety_snapshot.sequence++;
        rt_mutex_release(safety_lock);
    }

    return result;
}

rt_err_t cooling_safety_set_auto_control(rt_bool_t enabled)
{
    rt_err_t result;

    if (safety_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(safety_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        safety_snapshot.auto_control_enabled = enabled ? RT_TRUE : RT_FALSE;
        safety_snapshot.sequence++;
        rt_mutex_release(safety_lock);
    }

    return result;
}

rt_err_t cooling_safety_set_tank(rt_bool_t tank_present)
{
    rt_err_t result;

    if (safety_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(safety_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        safety_snapshot.tank_present = tank_present ? RT_TRUE : RT_FALSE;
        safety_snapshot.sequence++;
        rt_mutex_release(safety_lock);
    }

    return result;
}

rt_err_t cooling_safety_set_ota(rt_bool_t busy)
{
    rt_err_t result;

    if (safety_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(safety_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        safety_snapshot.ota_busy = busy ? RT_TRUE : RT_FALSE;
        safety_snapshot.sequence++;
        rt_mutex_release(safety_lock);
    }

    return result;
}

rt_bool_t cooling_safety_heater_allowed(void)
{
    rt_bool_t allowed = RT_FALSE;

    /* 未初始化即失败关闭。 */
    if (safety_lock == RT_NULL)
    {
        return RT_FALSE;
    }

    if (rt_mutex_take(safety_lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        allowed = (safety_snapshot.authorized &&
                   safety_snapshot.calibrated &&
                   safety_snapshot.power_on &&
                   safety_snapshot.auto_control_enabled &&
                   safety_snapshot.tank_present &&
                   !safety_snapshot.ota_busy) ? RT_TRUE : RT_FALSE;
        rt_mutex_release(safety_lock);
    }

    /* 读取失败同样按禁止加热处理。 */
    return allowed;
}
