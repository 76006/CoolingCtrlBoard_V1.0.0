/*
 * CoolingCtrlBoard 冷媒压力加热控制核心实现（纯 C，可移植）
 *
 * 状态机流程：
 *   插罐且数据有效 -> 满功率升压 -> 压力连续达到目标并保持确认时间
 *   -> 置“冷媒充足” -> 按上下滞回以低功率占空比维持。
 *   升压+确认总时长超过上限仍未达标 -> 锁存“冷媒不足”并关闭加热，
 *   直到拔罐才解除锁存。
 *
 * 本文件不依赖 RT-Thread、HAL、GPIO 或 ADC 驱动，全部计时为
 * 无符号整数饱和累加，不使用浮点。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_pressure_control.h"

#include <stddef.h>

void cooling_pressure_control_init(cooling_pressure_control_t *control)
{
    if (control == NULL)
    {
        return;
    }

    control->state = COOLING_PRESSURE_STATE_NOT_CALIBRATED;
    control->sufficient = false;
    control->low_latched = false;
    control->maintain_on = false;
    control->boost_elapsed_ms = 0U;
    control->confirm_elapsed_ms = 0U;
}

uint32_t cooling_pressure_time_add(uint32_t elapsed_ms, uint32_t delta_ms)
{
    if (delta_ms > (UINT32_MAX - elapsed_ms))
    {
        return UINT32_MAX;
    }

    return elapsed_ms + delta_ms;
}

void cooling_pressure_control_update(cooling_pressure_control_t *control,
                                     const cooling_pressure_input_t *input,
                                     cooling_pressure_output_t *output)
{
    uint32_t filtered;
    uint32_t target;
    uint32_t hyst_down;
    uint32_t hyst_up;

    if ((control == NULL) || (input == NULL) || (output == NULL))
    {
        return;
    }

    filtered = (uint32_t)input->filtered_adc;
    target = (uint32_t)COOLING_PRESSURE_TARGET_ADC;
    hyst_down = (uint32_t)COOLING_PRESSURE_HYSTERESIS_DOWN_ADC;
    hyst_up = (uint32_t)COOLING_PRESSURE_HYSTERESIS_UP_ADC;

    /* 默认输出：所有异常路径都保持 0% 占空比。 */
    output->duty_percent = 0U;
    output->state = control->state;
    output->interlock_mask = COOLING_PRESSURE_INTERLOCK_NONE;
    output->refrigerant_sufficient = control->sufficient;
    output->refrigerant_low = control->low_latched;

    /* 优先级 1：无罐 —— 立即关断并重置本轮全部进度与锁存。 */
    if (!input->tank_present)
    {
        control->state = COOLING_PRESSURE_STATE_NO_TANK;
        control->sufficient = false;
        control->low_latched = false;
        control->maintain_on = false;
        control->boost_elapsed_ms = 0U;
        control->confirm_elapsed_ms = 0U;
        output->state = control->state;
        output->refrigerant_sufficient = false;
        output->refrigerant_low = false;
        return;
    }

    /*
     * 优先级 2：未标定 / 未授权 / OTA 忙 / 未上电 / 自动控制禁用
     * —— 安全联锁，立即输出 0。只有主控上电且自动控制启用时才
     * 允许进入后续加热控制。
     */
    if (!input->calibrated)
    {
        output->interlock_mask |= COOLING_PRESSURE_INTERLOCK_NOT_CALIBRATED;
    }
    if (!input->authorized)
    {
        output->interlock_mask |= COOLING_PRESSURE_INTERLOCK_NOT_AUTHORIZED;
    }
    if (input->ota_busy)
    {
        output->interlock_mask |= COOLING_PRESSURE_INTERLOCK_OTA_BUSY;
    }
    if (!input->powered)
    {
        output->interlock_mask |= COOLING_PRESSURE_INTERLOCK_POWER_OFF;
    }
    if (!input->auto_control_enabled)
    {
        output->interlock_mask |= COOLING_PRESSURE_INTERLOCK_AUTO_DISABLED;
    }
    if (output->interlock_mask != COOLING_PRESSURE_INTERLOCK_NONE)
    {
        /* 未标定单独上报，其余联锁原因统一归入安全联锁状态。 */
        control->state = input->calibrated ?
            COOLING_PRESSURE_STATE_INTERLOCK :
            COOLING_PRESSURE_STATE_NOT_CALIBRATED;
        control->maintain_on = false;
        output->state = control->state;
        return;
    }

    /* 优先级 3：ADC 故障（无效、陈旧或超范围）—— 立即输出 0。 */
    if (!input->adc_valid)
    {
        control->state = COOLING_PRESSURE_STATE_SENSOR_FAULT;
        control->maintain_on = false;
        output->state = control->state;
        return;
    }

    /* 优先级 4：冷媒不足已锁存 —— 保持关断，直到拔罐清除锁存。 */
    if (control->low_latched)
    {
        control->state = COOLING_PRESSURE_STATE_LOW;
        control->maintain_on = false;
        output->state = control->state;
        output->refrigerant_sufficient = false;
        output->refrigerant_low = true;
        return;
    }

    /* 优先级 5：正常控制 —— 升压 -> 压力确认 -> 低功率维持。 */
    if (control->sufficient)
    {
        /* 已达到目标：按上下滞回用低功率占空比维持。
         * 低于下沿开启，高于上沿关闭，滞回带内保持上一次决策。 */
        control->state = COOLING_PRESSURE_STATE_MAINTAIN;
        if ((filtered + hyst_down) <= target)
        {
            control->maintain_on = true;
        }
        else if (filtered >= (target + hyst_up))
        {
            control->maintain_on = false;
        }
        output->duty_percent = control->maintain_on ?
            COOLING_PRESSURE_MAINTAIN_DUTY_PERCENT : 0U;
    }
    else
    {
        /* 升压计时只在实际执行控制的升压/确认阶段累计，
         * 联锁与故障期间暂停，避免故障时间误计入超时。 */
        control->boost_elapsed_ms =
            cooling_pressure_time_add(control->boost_elapsed_ms,
                                      input->dt_ms);
        if (control->boost_elapsed_ms >= COOLING_PRESSURE_MAX_BOOST_TIME_MS)
        {
            /* 升压总时长超时仍未达标：锁存冷媒不足并关闭加热。 */
            control->low_latched = true;
            control->maintain_on = false;
            control->state = COOLING_PRESSURE_STATE_LOW;
            output->state = control->state;
            output->refrigerant_sufficient = false;
            output->refrigerant_low = true;
            return;
        }

        if (filtered >= target)
        {
            /* 压力达到目标，但仍需连续保持确认时间才置充足。 */
            control->confirm_elapsed_ms =
                cooling_pressure_time_add(control->confirm_elapsed_ms,
                                          input->dt_ms);
            if (control->confirm_elapsed_ms >= COOLING_PRESSURE_CONFIRM_TIME_MS)
            {
                control->sufficient = true;
                control->maintain_on = false;
                control->confirm_elapsed_ms = 0U;
                control->state = COOLING_PRESSURE_STATE_MAINTAIN;
                output->refrigerant_sufficient = true;
                /* 刚确认达标：本轮先关闭，由维持滞回决定后续开关。 */
                output->duty_percent = 0U;
            }
            else
            {
                control->state = COOLING_PRESSURE_STATE_CONFIRM;
                output->duty_percent = COOLING_PRESSURE_BOOST_DUTY_PERCENT;
            }
        }
        else
        {
            /* 压力跌落：确认计时清零，继续满功率升压。 */
            control->confirm_elapsed_ms = 0U;
            control->state = COOLING_PRESSURE_STATE_BOOST;
            output->duty_percent = COOLING_PRESSURE_BOOST_DUTY_PERCENT;
        }
    }

    output->state = control->state;
    output->refrigerant_sufficient = control->sufficient;
    output->refrigerant_low = control->low_latched;
}
