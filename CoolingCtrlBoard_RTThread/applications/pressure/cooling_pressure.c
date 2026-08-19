/*
 * CoolingCtrlBoard pressure control.
 *
 * The pressure task reads the ADC and safety state, runs the pressure control
 * logic, and drives the heater with a fixed-window software PWM.  This module
 * is a singleton because the board has only one pressure sensor and heater.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_pressure.h"

#include "adc/cooling_adc.h"
#include "cooling_config.h"
#include "heater/cooling_heater.h"
#include "safety/cooling_safety.h"

static struct rt_mutex pressure_lock;
static cooling_pressure_snapshot_t pressure_snapshot;

static cooling_pressure_state_t pressure_state;
static rt_bool_t pressure_sufficient;
static rt_bool_t pressure_low_latched;
static rt_bool_t pressure_maintain_on;
static rt_uint32_t pressure_boost_elapsed_ms;
static rt_uint32_t pressure_confirm_elapsed_ms;
static rt_uint32_t pressure_sequence;

static rt_uint32_t pressure_ticks_to_ms(rt_tick_t ticks)
{
    return (rt_uint32_t)((ticks * 1000U) /
                         (rt_tick_t)RT_TICK_PER_SECOND);
}

static void pressure_reset(void)
{
    pressure_state = COOLING_PRESSURE_STATE_STOP;
    pressure_sufficient = RT_FALSE;
    pressure_low_latched = RT_FALSE;
    pressure_maintain_on = RT_FALSE;
    pressure_boost_elapsed_ms = 0U;
    pressure_confirm_elapsed_ms = 0U;
}

/* Return the heater duty for this control period. */
static rt_uint8_t pressure_update(rt_uint16_t filtered_adc,
                                 rt_uint32_t block_reason,
                                 rt_uint32_t dt_ms)
{
    rt_uint32_t filtered = filtered_adc;
    rt_uint32_t target = COOLING_PRESSURE_TARGET_ADC;

    if ((block_reason & COOLING_PRESSURE_BLOCK_NO_TANK) != 0U)
    {
        pressure_reset();
        return 0U;
    }

    if (block_reason != COOLING_PRESSURE_BLOCK_NONE)
    {
        pressure_state = COOLING_PRESSURE_STATE_STOP;
        pressure_maintain_on = RT_FALSE;
        return 0U;
    }

    if (pressure_low_latched)
    {
        pressure_state = COOLING_PRESSURE_STATE_FAULT;
        pressure_maintain_on = RT_FALSE;
        return 0U;
    }

    if (pressure_sufficient)
    {
        pressure_state = COOLING_PRESSURE_STATE_HOLD;

        if ((filtered + COOLING_PRESSURE_HYSTERESIS_DOWN_ADC) <= target)
        {
            pressure_maintain_on = RT_TRUE;
        }
        else if (filtered >=
                 (target + COOLING_PRESSURE_HYSTERESIS_UP_ADC))
        {
            pressure_maintain_on = RT_FALSE;
        }

        return pressure_maintain_on ?
            COOLING_PRESSURE_MAINTAIN_DUTY_PERCENT : 0U;
    }

    pressure_state = COOLING_PRESSURE_STATE_HEATING;
    pressure_boost_elapsed_ms += dt_ms;
    if (pressure_boost_elapsed_ms >= COOLING_PRESSURE_MAX_BOOST_TIME_MS)
    {
        pressure_low_latched = RT_TRUE;
        pressure_maintain_on = RT_FALSE;
        pressure_state = COOLING_PRESSURE_STATE_FAULT;
        return 0U;
    }

    if (filtered >= target)
    {
        pressure_confirm_elapsed_ms += dt_ms;
        if (pressure_confirm_elapsed_ms >=
            COOLING_PRESSURE_CONFIRM_TIME_MS)
        {
            pressure_sufficient = RT_TRUE;
            pressure_maintain_on = RT_FALSE;
            pressure_confirm_elapsed_ms = 0U;
            pressure_state = COOLING_PRESSURE_STATE_HOLD;
            return 0U;
        }
    }
    else
    {
        pressure_confirm_elapsed_ms = 0U;
    }

    return COOLING_PRESSURE_BOOST_DUTY_PERCENT;
}

static void pressure_control_entry(void *parameter)
{
    rt_tick_t last_tick = rt_tick_get();
    rt_bool_t last_heater_level = RT_FALSE;
    rt_uint32_t window_elapsed_ms = 0U;

    (void)parameter;

    while (1)
    {
        cooling_pressure_snapshot_t next;
        cooling_adc_snapshot_t adc;
        cooling_safety_snapshot_t safety;
        cooling_heater_snapshot_t heater;
        rt_tick_t now = rt_tick_get();
        rt_bool_t adc_valid = RT_FALSE;
        rt_uint32_t adc_age_ms;
        rt_uint32_t block_reason = COOLING_PRESSURE_BLOCK_NONE;
        rt_uint32_t dt_ms;
        rt_uint32_t on_time_ms;
        rt_uint8_t duty_percent;

        rt_memset(&next, 0, sizeof(next));
        next.sequence = ++pressure_sequence;
        next.tick = now;

        cooling_safety_get_snapshot(&safety);
        next.tank_present = safety.tank_present;

        cooling_heater_get_snapshot(&heater);
        next.heater_on = heater.heater_on;

        cooling_adc_get_snapshot(&adc);
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

        if (!safety.calibrated)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_NOT_CALIBRATED;
        }
        if (!safety.authorized)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_UNAUTHORIZED;
        }
        if (safety.ota_busy)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_OTA_BUSY;
        }
        if (!safety.power_on)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_POWER_OFF;
        }
        if (!safety.auto_control_enabled)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_AUTO_DISABLED;
        }
        if (!safety.tank_present)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_NO_TANK;
        }
        if (!adc_valid)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_ADC_INVALID;
        }

        dt_ms = pressure_ticks_to_ms(now - last_tick);
        last_tick = now;
        duty_percent = pressure_update(next.filtered, block_reason, dt_ms);

        if (duty_percent == 0U)
        {
            window_elapsed_ms = 0U;
            if (last_heater_level)
            {
                (void)cooling_heater_set(RT_FALSE);
            }
            last_heater_level = RT_FALSE;
            next.heater_on = RT_FALSE;
        }
        else
        {
            rt_bool_t heater_level;

            window_elapsed_ms += dt_ms;
            if (window_elapsed_ms >= COOLING_PRESSURE_PWM_WINDOW_MS)
            {
                window_elapsed_ms %= COOLING_PRESSURE_PWM_WINDOW_MS;
            }
            on_time_ms = ((rt_uint32_t)duty_percent *
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
                    last_heater_level = RT_FALSE;
                    next.heater_on = RT_FALSE;
                }
            }
        }

        next.state = pressure_state;
        next.duty_percent = duty_percent;
        next.refrigerant_sufficient = pressure_sufficient;
        next.refrigerant_low = pressure_low_latched;
        next.block_reason = block_reason;

        rt_mutex_take(&pressure_lock, RT_WAITING_FOREVER);
        pressure_snapshot = next;
        rt_mutex_release(&pressure_lock);

        rt_thread_mdelay(COOLING_PRESSURE_CONTROL_PERIOD_MS);
    }
}

rt_err_t cooling_pressure_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    rt_mutex_init(&pressure_lock, "prslock", RT_IPC_FLAG_PRIO);
    rt_memset(&pressure_snapshot, 0, sizeof(pressure_snapshot));
    pressure_sequence = 0U;
    pressure_reset();

    thread = rt_thread_create("pressctl", pressure_control_entry, RT_NULL,
                              COOLING_PRESSURE_THREAD_STACK_SIZE,
                              COOLING_PRESSURE_THREAD_PRIORITY,
                              COOLING_PRESSURE_THREAD_TIMESLICE);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(thread);
        return result;
    }

    return RT_EOK;
}

void cooling_pressure_get_snapshot(cooling_pressure_snapshot_t *snapshot)
{
    rt_mutex_take(&pressure_lock, RT_WAITING_FOREVER);
    *snapshot = pressure_snapshot;
    rt_mutex_release(&pressure_lock);
}

#ifdef RT_USING_FINSH
static const char *const pressure_state_name[COOLING_PRESSURE_STATE_COUNT] =
{
    "stop",
    "heating",
    "hold",
    "fault"
};

static void pressure_status(int argc, char **argv)
{
    cooling_pressure_snapshot_t snapshot;

    (void)argc;
    (void)argv;

    cooling_pressure_get_snapshot(&snapshot);

    rt_kprintf("pressure seq=%u tick=%u state=%s duty=%u heater=%s "
               "tank=%s adc=%s\n",
               (unsigned int)snapshot.sequence,
               (unsigned int)snapshot.tick,
               pressure_state_name[snapshot.state],
               (unsigned int)snapshot.duty_percent,
               snapshot.heater_on ? "on" : "off",
               snapshot.tank_present ? "present" : "absent",
               snapshot.adc_valid ? "valid" : "invalid");
    rt_kprintf("  hysn2: raw=%u filtered=%u mv=%u\n",
               (unsigned int)snapshot.raw,
               (unsigned int)snapshot.filtered,
               (unsigned int)snapshot.millivolts);
    rt_kprintf("  sufficient=%s low=%s block=0x%08x\n",
               snapshot.refrigerant_sufficient ? "yes" : "no",
               snapshot.refrigerant_low ? "yes" : "no",
               (unsigned int)snapshot.block_reason);
}
MSH_CMD_EXPORT(pressure_status, show pressure control state (read only));
#endif
