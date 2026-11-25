/*
 * CoolingCtrlBoard 统一安全模块实现
 *
 * 本模块只保存状态并做线程安全发布，不含任何业务策略：各条件字段
 * 由生产者（命令、罐体、OTA、启动流程）通过 setter 发布，加热模块
 * 通过 cooling_safety_heater_allowed() 做唯一裁决。
 *
 * 初始状态默认失败关闭。
 * 本文件不包含任何业务模块头文件，防止安全模块反向依赖业务层。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_safety.h"

#include "cooling_config.h"

static struct rt_mutex safety_lock;
static cooling_safety_snapshot_t safety_snapshot;
#if COOLING_TEST_MODE
static rt_bool_t test_heat_enabled;
static rt_uint32_t test_heat_trip;
#endif

void cooling_safety_init(void)
{
    rt_mutex_init(&safety_lock, "safelock", RT_IPC_FLAG_PRIO);

    /* 默认失败关闭：authorized/power/tank/ota 全部为假。 */
    rt_memset(&safety_snapshot, 0, sizeof(safety_snapshot));
    safety_snapshot.auto_control_enabled = RT_TRUE;
    safety_snapshot.calibrated = COOLING_PRESSURE_CALIBRATED != 0U;
}

void cooling_safety_get_snapshot(cooling_safety_snapshot_t *snapshot)
{
    rt_mutex_take(&safety_lock, RT_WAITING_FOREVER);
    *snapshot = safety_snapshot;
    rt_mutex_release(&safety_lock);
}

static void safety_set(rt_bool_t *field, rt_bool_t value, rt_bool_t unsafe)
{
    rt_mutex_take(&safety_lock, RT_WAITING_FOREVER);
    if (*field != value && value == unsafe)
        ++safety_snapshot.trip_sequence;
    *field = value;
    safety_snapshot.sequence++;
    rt_mutex_release(&safety_lock);
}

void cooling_safety_set_authorized(rt_bool_t authorized)
{
    safety_set(&safety_snapshot.authorized, authorized, RT_FALSE);
}

void cooling_safety_set_power(rt_bool_t power_on)
{
    safety_set(&safety_snapshot.power_on, power_on, RT_FALSE);
}

void cooling_safety_set_auto_control(rt_bool_t enabled)
{
    safety_set(&safety_snapshot.auto_control_enabled, enabled, RT_FALSE);
}

void cooling_safety_set_tank(rt_bool_t tank_present)
{
    safety_set(&safety_snapshot.tank_present, tank_present, RT_FALSE);
}

void cooling_safety_set_ota(rt_bool_t busy)
{
    safety_set(&safety_snapshot.ota_busy, busy, RT_TRUE);
}

#if COOLING_TEST_MODE
void cooling_safety_set_test_heat(rt_bool_t enable, rt_uint32_t sequence)
{
    rt_mutex_take(&safety_lock, RT_WAITING_FOREVER);
    test_heat_enabled = enable;
    test_heat_trip = sequence;
    rt_mutex_release(&safety_lock);
}
#endif

rt_bool_t cooling_safety_heater_allowed(void)
{
    rt_bool_t allowed = RT_FALSE;

    rt_mutex_take(&safety_lock, RT_WAITING_FOREVER);
    allowed = safety_snapshot.authorized &&
              safety_snapshot.calibrated &&
              safety_snapshot.power_on &&
              safety_snapshot.auto_control_enabled &&
              safety_snapshot.tank_present &&
              !safety_snapshot.ota_busy;
#if COOLING_TEST_MODE
    allowed = allowed && test_heat_enabled &&
              test_heat_trip == safety_snapshot.trip_sequence;
#endif
    rt_mutex_release(&safety_lock);

    return allowed;
}
