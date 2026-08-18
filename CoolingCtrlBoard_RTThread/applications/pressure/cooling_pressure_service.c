/*
 * CoolingCtrlBoard 冷媒压力加热控制服务
 *
 * 本层负责输入采集与输出执行，不做控制策略判断：
 *   - 输入：ADC 快照（HYSN2）与统一安全快照（授权/标定/上电/自动
 *     控制/罐体存在/OTA 忙，全部取自 safety 模块，不再直接包含
 *     command、OTA、UID 或罐体模块）、加热模块快照（实际输出），
 *     全部转换为纯 C 控制核心的输入结构体；
 *   - 输出：核心给出的 0..100 占空比经固定窗口软件 PWM 转换为
 *     cooling_heater_set() 的开关命令。无罐、联锁、无效 ADC 或
 *     0% 命令都立即关断，不等待 PWM 窗口。
 *
 * 加热输出的全部写入都通过加热模块公共接口，不绕过板级适配。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_pressure.h"

/* 纯 C 控制核心为模块私有实现，仅本服务包含。 */
#include "cooling_pressure_control.h"

#include "cooling_adc.h"
#include "cooling_config.h"
#include "cooling_sensor_config.h"
#include "heater/cooling_heater.h"
#include "safety/cooling_safety.h"

static rt_thread_t pressure_thread;
static rt_mutex_t pressure_lock;
static cooling_pressure_snapshot_t pressure_snapshot;
static cooling_pressure_control_t pressure_control;
/* 控制线程持有并顺序更新的“上一次发布内容”，用于跨周期保留
 * 开关等慢变量；其他线程只能通过 getter 读取 pressure_snapshot。 */
static cooling_pressure_snapshot_t last_published;

/*
 * 把 tick 差值换算成毫秒。RT_TICK_PER_SECOND 为 1000 时即恒等；
 * 乘法前先钳位，防止长时间阻塞后的 tick 差值溢出 32 位。
 */
static uint32_t pressure_ticks_to_ms(rt_tick_t ticks)
{
    rt_tick_t cap = (rt_tick_t)(UINT32_MAX / 1000U);

    if (ticks > cap)
    {
        return UINT32_MAX;
    }

    return (uint32_t)((ticks * 1000U) / (rt_tick_t)RT_TICK_PER_SECOND);
}

static void pressure_control_entry(void *parameter)
{
    rt_tick_t last_tick;
    rt_bool_t last_heater_level = RT_FALSE;
    uint32_t window_elapsed_ms = 0U;

    (void)parameter;

    /* 服务启动后先确保加热关闭，再进入控制循环。 */
    (void)cooling_heater_set(RT_FALSE);
    last_tick = rt_tick_get();

    while (1)
    {
        cooling_pressure_snapshot_t next;
        cooling_adc_snapshot_t adc;
        cooling_safety_snapshot_t safety;
        cooling_heater_snapshot_t heater;
        cooling_pressure_input_t input;
        cooling_pressure_output_t output;
        rt_tick_t now = rt_tick_get();
        rt_bool_t safety_ok = RT_FALSE;
        rt_bool_t adc_valid = RT_FALSE;
        rt_bool_t tank_present = RT_FALSE;
        uint32_t dt_ms;
        uint32_t on_time_ms;

        next = last_published;
        next.sequence++;
        next.tick = now;
        next.adc_valid = RT_FALSE;
        next.tank_present = RT_FALSE;

        /*
         * 统一安全快照：授权/标定/上电/自动控制/罐体存在/OTA 忙
         * 全部由安全模块发布；读取失败按失败关闭默认值处理。
         */
        if (cooling_safety_get_snapshot(&safety) == RT_EOK)
        {
            safety_ok = RT_TRUE;
            /* 罐体存在来自安全模块的消抖后状态。 */
            tank_present = safety.tank_present ? RT_TRUE : RT_FALSE;
            next.tank_present = tank_present;
        }

        /* 加热实际输出状态经加热模块快照读取。 */
        if (cooling_heater_get_snapshot(&heater) == RT_EOK)
        {
            next.heater_on = heater.heater_on ? RT_TRUE : RT_FALSE;
        }

        if (cooling_adc_get_snapshot(&adc) == RT_EOK)
        {
            uint32_t adc_age_ms;

            /* 有效位 + 新鲜度 + 合理范围三重校验。 */
            adc_age_ms = pressure_ticks_to_ms(now - adc.tick);
            if (((adc.valid_mask &
                  COOLING_ADC_VALID_BIT(COOLING_ADC_HYSN2)) != 0U) &&
                (adc_age_ms <= COOLING_PRESSURE_ADC_MAX_AGE_MS) &&
                (adc.value[COOLING_ADC_HYSN2].filtered >=
                 COOLING_PRESSURE_ADC_MIN_FILTERED) &&
                (adc.value[COOLING_ADC_HYSN2].filtered <=
                 COOLING_PRESSURE_ADC_MAX_FILTERED))
            {
                adc_valid = RT_TRUE;
            }

            next.raw = adc.value[COOLING_ADC_HYSN2].raw;
            next.filtered = adc.value[COOLING_ADC_HYSN2].filtered;
            next.millivolts = adc.value[COOLING_ADC_HYSN2].millivolts;
            next.adc_valid = adc_valid;
        }

        /* 输入组装：所有安全条件都汇入纯 C 核心统一判定。 */
        dt_ms = pressure_ticks_to_ms(now - last_tick);
        last_tick = now;
        input.calibrated = safety_ok ? (safety.calibrated != 0U)
                                     : (COOLING_PRESSURE_CALIBRATED != 0U);
        input.tank_present = tank_present;
        input.adc_valid = adc_valid;
        input.filtered_adc = next.filtered;
        input.authorized = safety_ok ? (safety.authorized != 0U) : false;
        input.ota_busy = safety_ok ? (safety.ota_busy != 0U) : false;
        input.powered = safety_ok ? (safety.power_on != 0U) : false;
        input.auto_control_enabled = safety_ok
                                     ? (safety.auto_control_enabled != 0U)
                                     : true;
        input.dt_ms = dt_ms;
        cooling_pressure_control_update(&pressure_control, &input, &output);

        /*
         * 固定窗口软件 PWM：窗口内前 duty% 时间开启。
         * 0% 命令（含无罐、联锁、无效 ADC）必须本轮立即关断，
         * 不得等待窗口结束；窗口相位同时清零，使下一次加热从
         * 窗口起点开始，保证实际占空比不高于命令值。
         */
        if (output.duty_percent == 0U)
        {
            window_elapsed_ms = 0U;
            /* 每个安全周期都明确写关，避免其他调用者改变输出后残留。 */
            (void)cooling_heater_set(RT_FALSE);
            last_heater_level = RT_FALSE;
            next.heater_on = RT_FALSE;
        }
        else
        {
            rt_bool_t heater_level;

            window_elapsed_ms =
                cooling_pressure_time_add(window_elapsed_ms, dt_ms);
            if (window_elapsed_ms >= COOLING_PRESSURE_PWM_WINDOW_MS)
            {
                window_elapsed_ms %= COOLING_PRESSURE_PWM_WINDOW_MS;
            }
            on_time_ms = ((uint32_t)output.duty_percent *
                          COOLING_PRESSURE_PWM_WINDOW_MS) / 100U;
            heater_level = (window_elapsed_ms <= on_time_ms) ?
                RT_TRUE : RT_FALSE;
            if (heater_level != last_heater_level)
            {
                if (cooling_heater_set(heater_level) == RT_EOK)
                {
                    last_heater_level = heater_level;
                    next.heater_on = heater_level;
                }
                else
                {
                    /* 执行层拒绝或写入失败时按安全状态处理，并在下周期重试。 */
                    (void)cooling_heater_set(RT_FALSE);
                    last_heater_level = RT_FALSE;
                    next.heater_on = RT_FALSE;
                }
            }
        }

        next.state = output.state;
        next.duty_percent = output.duty_percent;
        next.refrigerant_sufficient =
            output.refrigerant_sufficient ? RT_TRUE : RT_FALSE;
        next.refrigerant_low = output.refrigerant_low ? RT_TRUE : RT_FALSE;
        next.interlock_mask = output.interlock_mask;
        last_published = next;

        /* 互斥量只保护一次结构体复制，锁定时间很短。 */
        if (rt_mutex_take(pressure_lock, RT_WAITING_FOREVER) == RT_EOK)
        {
            pressure_snapshot = next;
            rt_mutex_release(pressure_lock);
        }

        rt_thread_mdelay(COOLING_PRESSURE_CONTROL_PERIOD_MS);
    }
}

rt_err_t cooling_pressure_start(void)
{
    rt_err_t result;

    /* 重复调用幂等，防止创建多个 PWM 任务互相打架。 */
    if (pressure_thread != RT_NULL)
    {
        return RT_EOK;
    }

    pressure_lock = rt_mutex_create("prslock", RT_IPC_FLAG_PRIO);
    if (pressure_lock == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_memset(&pressure_snapshot, 0, sizeof(pressure_snapshot));
    rt_memset(&last_published, 0, sizeof(last_published));
    cooling_pressure_control_init(&pressure_control);

    pressure_thread = rt_thread_create("pressctl",
                                       pressure_control_entry,
                                       RT_NULL,
                                       COOLING_PRESSURE_THREAD_STACK_SIZE,
                                       COOLING_PRESSURE_THREAD_PRIORITY,
                                       COOLING_PRESSURE_THREAD_TIMESLICE);
    if (pressure_thread == RT_NULL)
    {
        rt_mutex_delete(pressure_lock);
        pressure_lock = RT_NULL;
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(pressure_thread);
    if (result != RT_EOK)
    {
        /* 失败路径确保加热关闭。 */
        (void)cooling_heater_set(RT_FALSE);
        rt_thread_delete(pressure_thread);
        pressure_thread = RT_NULL;
        rt_mutex_delete(pressure_lock);
        pressure_lock = RT_NULL;
        return result;
    }

    return RT_EOK;
}

rt_err_t cooling_pressure_get_snapshot(cooling_pressure_snapshot_t *snapshot)
{
    rt_err_t result;

    if (snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (pressure_lock == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_mutex_take(pressure_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        *snapshot = pressure_snapshot;
        rt_mutex_release(pressure_lock);
    }

    return result;
}

#ifdef RT_USING_FINSH
/* 只读状态命令：在 MSH 中输入 pressure_status 查看控制状态。
 * 刻意不提供任何手动开加热命令，避免绕过联锁。 */
static const char *const pressure_state_name[COOLING_PRESSURE_STATE_COUNT] =
{
    "not_calibrated",
    "no_tank",
    "boost",
    "confirm",
    "maintain",
    "refrigerant_low",
    "sensor_fault",
    "interlock"
};

static void pressure_status(int argc, char **argv)
{
    cooling_pressure_snapshot_t snapshot;
    const char *state_name;

    (void)argc;
    (void)argv;

    if (cooling_pressure_get_snapshot(&snapshot) != RT_EOK)
    {
        rt_kprintf("pressure control is not ready\n");
        return;
    }

    state_name = ((unsigned int)snapshot.state <
                  COOLING_PRESSURE_STATE_COUNT) ?
        pressure_state_name[snapshot.state] : "unknown";

    rt_kprintf("pressure seq=%u tick=%u state=%s duty=%u heater=%s "
               "tank=%s adc=%s\n",
               (unsigned int)snapshot.sequence,
               (unsigned int)snapshot.tick,
               state_name,
               (unsigned int)snapshot.duty_percent,
               snapshot.heater_on ? "on" : "off",
               snapshot.tank_present ? "present" : "absent",
               snapshot.adc_valid ? "valid" : "invalid");
    rt_kprintf("  hysn2: raw=%u filtered=%u mv=%u\n",
               (unsigned int)snapshot.raw,
               (unsigned int)snapshot.filtered,
               (unsigned int)snapshot.millivolts);
    rt_kprintf("  sufficient=%s low=%s interlock=0x%02x\n",
               snapshot.refrigerant_sufficient ? "yes" : "no",
               snapshot.refrigerant_low ? "yes" : "no",
               (unsigned int)snapshot.interlock_mask);
}
MSH_CMD_EXPORT(pressure_status, show pressure control state (read only));
#endif
