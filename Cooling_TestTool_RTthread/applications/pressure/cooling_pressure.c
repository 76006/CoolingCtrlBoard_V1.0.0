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
#if COOLING_TEST_MODE
#include "relief_service.h"
#endif

static struct rt_mutex pressure_lock;
static cooling_pressure_snapshot_t pressure_snapshot;
static cooling_pressure_targets_t pressure_targets;

static cooling_pressure_state_t pressure_state;
static rt_bool_t pressure_sufficient;
static rt_bool_t pressure_low_latched;
static rt_bool_t pressure_over_latched;
static rt_bool_t temperature_over_latched;
static rt_bool_t pressure_maintain_on;
static rt_bool_t pressure_warmup_complete;
static rt_uint32_t pressure_boost_elapsed_ms;
static rt_uint32_t pressure_sequence;

#ifdef RT_USING_FINSH
static volatile rt_bool_t pressure_print_enabled = RT_FALSE;
static void pressure_print_snapshot(const cooling_pressure_snapshot_t *snapshot);
#endif

static rt_uint32_t pressure_ticks_to_ms(rt_tick_t ticks)
{
    return (rt_uint32_t)(((rt_uint64_t)ticks * 1000U) / RT_TICK_PER_SECOND);
}

/* 与默认目标的偏移保持不变；低目标时钳位到零，避免无符号下溢。 */
static rt_uint16_t pressure_threshold_adc(rt_uint16_t target_mpa1000,
                                         rt_int32_t offset_mpa1000)
{
    rt_int32_t value = (rt_int32_t)target_mpa1000 + offset_mpa1000;

    if (value < 0)
    {
        value = 0;
    }
    if (value > (rt_int32_t)COOLING_PRESSURE_FULL_MPA1000)
    {
        value = COOLING_PRESSURE_FULL_MPA1000;
    }
    return (rt_uint16_t)(COOLING_PRESSURE_ADC_ZERO +
        (((rt_uint32_t)value *
          (COOLING_PRESSURE_ADC_FULL - COOLING_PRESSURE_ADC_ZERO) +
          COOLING_PRESSURE_FULL_MPA1000 / 2U) /
         COOLING_PRESSURE_FULL_MPA1000));
}

static void pressure_reset(void)
{
    pressure_state = COOLING_PRESSURE_STATE_STOP;
    pressure_sufficient = RT_FALSE;
    pressure_low_latched = RT_FALSE;
    pressure_over_latched = RT_FALSE;
    temperature_over_latched = RT_FALSE;
    pressure_maintain_on = RT_FALSE;
    pressure_warmup_complete = RT_FALSE;
    pressure_boost_elapsed_ms = 0U;
}

/* Return the heater duty for this control period. */
static rt_uint8_t pressure_update(rt_uint16_t raw_adc,
                                 rt_uint16_t filtered_adc,
                                 rt_int16_t temperature_deg10,
                                 rt_uint32_t block_reason,
                                 rt_uint32_t dt_ms,
                                 const cooling_pressure_targets_t *targets)
{
    rt_uint32_t raw = raw_adc;
    rt_uint32_t filtered = filtered_adc;
    rt_uint32_t pressure = (raw > filtered) ? raw : filtered;
    rt_uint16_t reheat_adc = pressure_threshold_adc(targets->pressure_mpa1000,
        -(rt_int32_t)COOLING_PRESSURE_REHEAT_MARGIN_MPA1000);
    rt_uint16_t fast_end_adc = pressure_threshold_adc(targets->pressure_mpa1000,
        -(rt_int32_t)COOLING_PRESSURE_FAST_MARGIN_MPA1000);
    rt_uint16_t slow_start_adc = pressure_threshold_adc(targets->pressure_mpa1000,
        -(rt_int32_t)COOLING_PRESSURE_SLOW_MARGIN_MPA1000);
#if COOLING_TEST_MODE
    rt_uint16_t high_adc = pressure_threshold_adc(targets->pressure_mpa1000, 0);

    (void)temperature_deg10;
    (void)dt_ms;
#else
    rt_uint16_t high_adc = pressure_threshold_adc(targets->pressure_mpa1000,
        (rt_int32_t)COOLING_PRESSURE_STOP_OFFSET_MPA1000);
#endif

    if ((block_reason & COOLING_PRESSURE_BLOCK_NO_TANK) != 0U)
    {
        pressure_reset();
        return 0U;
    }

    if (block_reason != COOLING_PRESSURE_BLOCK_NONE)
    {
        pressure_state =
            ((block_reason & (COOLING_PRESSURE_BLOCK_OVERPRESSURE |
                              COOLING_PRESSURE_BLOCK_OVERTEMPERATURE)) != 0U) ?
            COOLING_PRESSURE_STATE_FAULT : COOLING_PRESSURE_STATE_STOP;
        pressure_maintain_on = RT_FALSE;
        return 0U;
    }

    if (pressure_low_latched)
    {
        pressure_state = COOLING_PRESSURE_STATE_FAULT;
        pressure_maintain_on = RT_FALSE;
        return 0U;
    }

    /* 测试模式在目标值停热；正常模式保留相对目标 +0.005 MPa 的停热点。 */
    if (pressure >= high_adc)
    {
        pressure_state = COOLING_PRESSURE_STATE_HOLD;
        pressure_sufficient = RT_TRUE;
#if !COOLING_TEST_MODE
        pressure_warmup_complete = RT_TRUE;
#endif
        pressure_maintain_on = RT_FALSE;
        return 0U;
    }

    /* 正常模式在目标下方 0.025 MPa 进入压力保持阶段。 */
#if !COOLING_TEST_MODE
    if (!pressure_sufficient &&
        (pressure >= reheat_adc))
    {
        pressure_sufficient = RT_TRUE;
        pressure_warmup_complete = RT_TRUE;
        pressure_maintain_on = RT_TRUE;
    }

    if (!pressure_sufficient)
    {
        pressure_state = COOLING_PRESSURE_STATE_HEATING;
        pressure_boost_elapsed_ms += dt_ms;
        if (pressure_boost_elapsed_ms >= COOLING_PRESSURE_MAX_BOOST_TIME_MS)
        {
            pressure_low_latched = RT_TRUE;
            pressure_maintain_on = RT_FALSE;
            pressure_state = COOLING_PRESSURE_STATE_FAULT;
            return 0U;
        }

        /* 先用满功率升到运行时目标温度。 */
        if (!pressure_warmup_complete)
        {
            if (temperature_deg10 < targets->temperature_deg10)
            {
                return COOLING_WARMUP_DUTY_PERCENT;
            }
            pressure_warmup_complete = RT_TRUE;
            pressure_maintain_on = RT_TRUE;
        }

        /*
         * 温度先到目标但压力不足时，在目标温度附近保温。
         * 五分钟计时继续累计，达到压力目标或超时才退出。
         */
        if (temperature_deg10 <=
            (targets->temperature_deg10 -
             COOLING_WARMUP_TEMP_HYSTERESIS_DEG10))
        {
            pressure_maintain_on = RT_TRUE;
        }
        else if (temperature_deg10 >=
                 (targets->temperature_deg10 +
                  COOLING_WARMUP_TEMP_HYSTERESIS_DEG10))
        {
            pressure_maintain_on = RT_FALSE;
        }

        return pressure_maintain_on ?
            COOLING_WARMUP_HOLD_DUTY_PERCENT : 0U;
    }
    else
#else
    /* 测试模式没有预热温度门槛，也不累计未达压超时。 */
    if (!pressure_sufficient)
    {
        pressure_state = COOLING_PRESSURE_STATE_HEATING;
        pressure_maintain_on = RT_TRUE;
    }
    else
#endif
    {
        pressure_state = COOLING_PRESSURE_STATE_HOLD;

        /* 回落到目标下方 0.025 MPa 后恢复补热。 */
        if (!pressure_maintain_on)
        {
            if (pressure > reheat_adc)
            {
                return 0U;
            }
            pressure_maintain_on = RT_TRUE;
        }
    }

    if (pressure < fast_end_adc)
    {
        return COOLING_PRESSURE_FAST_DUTY_PERCENT;
    }
    if (pressure < slow_start_adc)
    {
        return COOLING_PRESSURE_MEDIUM_DUTY_PERCENT;
    }
    return COOLING_PRESSURE_SLOW_DUTY_PERCENT;
}

static void pressure_control_entry(void *parameter)
{
    rt_tick_t last_tick = rt_tick_get();
    rt_bool_t last_heater_level = RT_FALSE;
    rt_bool_t last_power_on = RT_FALSE;
    rt_uint32_t window_elapsed_ms = 0U;
    rt_uint32_t last_targets_sequence = 0U;

#ifdef RT_USING_FINSH
    rt_tick_t last_print_tick = last_tick;
#endif

    (void)parameter;

    while (1)
    {
        cooling_pressure_snapshot_t next;
        cooling_adc_snapshot_t adc;
        cooling_safety_snapshot_t safety;
        cooling_heater_snapshot_t heater;
        cooling_pressure_targets_t targets;
        rt_tick_t now = rt_tick_get();
        rt_bool_t adc_valid = RT_FALSE;
        rt_bool_t temperature_valid = RT_FALSE;
        rt_int16_t temperature_deg10 = 0;
#if COOLING_TEST_MODE
        rt_int16_t raw_temperature_deg10 = 0;
#endif
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
        cooling_pressure_get_targets(&targets);

        /* 新一轮开启或目标变化时重启控制阶段，保留故障锁存。 */
        if ((safety.power_on && !last_power_on) ||
            (targets.sequence != last_targets_sequence))
        {
            pressure_sufficient = RT_FALSE;
            pressure_maintain_on = RT_FALSE;
            pressure_warmup_complete = RT_FALSE;
            pressure_boost_elapsed_ms = 0U;
        }
        last_power_on = safety.power_on;
        last_targets_sequence = targets.sequence;

        cooling_heater_get_snapshot(&heater);
        next.heater_on = heater.heater_on;

        cooling_adc_get_snapshot(&adc);
        now = rt_tick_get();
        adc_age_ms = pressure_ticks_to_ms(now - adc.tick);
        if (((adc.valid_mask &
              COOLING_ADC_VALID_BIT(COOLING_ADC_HYSN2)) != 0U) &&
            (adc_age_ms <= COOLING_PRESSURE_ADC_MAX_AGE_MS)
#if COOLING_TEST_MODE
            && (adc.value[COOLING_ADC_HYSN2].raw >=
                COOLING_PRESSURE_ADC_ZERO - COOLING_TEST_PRESSURE_ADC_MARGIN)
            && (adc.value[COOLING_ADC_HYSN2].raw <=
                COOLING_PRESSURE_ADC_FULL + COOLING_TEST_PRESSURE_ADC_MARGIN)
            && (adc.value[COOLING_ADC_HYSN2].filtered >=
                COOLING_PRESSURE_ADC_ZERO - COOLING_TEST_PRESSURE_ADC_MARGIN)
            && (adc.value[COOLING_ADC_HYSN2].filtered <=
                COOLING_PRESSURE_ADC_FULL + COOLING_TEST_PRESSURE_ADC_MARGIN)
#else
            &&
            (adc.value[COOLING_ADC_HYSN2].filtered >=
             COOLING_PRESSURE_ADC_MIN_FILTERED) &&
            (adc.value[COOLING_ADC_HYSN2].filtered <=
             COOLING_PRESSURE_ADC_MAX_FILTERED)
#endif
            )
        {
            adc_valid = RT_TRUE;
        }
        if (((adc.valid_mask &
              COOLING_ADC_VALID_BIT(COOLING_ADC_NTC1)) != 0U) &&
            (adc_age_ms <= COOLING_PRESSURE_ADC_MAX_AGE_MS) &&
#if COOLING_TEST_MODE
            (cooling_ntc_from_adc(COOLING_ADC_NTC1,
                adc.value[COOLING_ADC_NTC1].raw, &raw_temperature_deg10) == 0) &&
            (cooling_ntc_from_adc(COOLING_ADC_NTC1,
                adc.value[COOLING_ADC_NTC1].filtered,
#else
            (cooling_sensor_ntc1_temperature_deg10(
#endif
                &temperature_deg10) == 0))
        {
            temperature_valid = RT_TRUE;
        }

        next.raw = adc.value[COOLING_ADC_HYSN2].raw;
        next.filtered = adc.value[COOLING_ADC_HYSN2].filtered;
        next.millivolts = adc.value[COOLING_ADC_HYSN2].millivolts;
        next.adc_valid = adc_valid;
        next.temperature_deg10 = temperature_deg10;
        next.temperature_valid = temperature_valid;

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
        if (!temperature_valid)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_TEMP_INVALID;
        }

        /* 使用未滤波值做硬关断，避免滤波延迟造成超压。 */
        if (adc_valid &&
            (adc.value[COOLING_ADC_HYSN2].raw >=
             COOLING_PRESSURE_CUTOFF_ADC))
        {
            pressure_over_latched = RT_TRUE;
        }
        if (pressure_over_latched)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_OVERPRESSURE;
        }

        if (temperature_valid &&
            ((temperature_deg10 >= COOLING_TEMPERATURE_CUTOFF_DEG10)
#if COOLING_TEST_MODE
            || (raw_temperature_deg10 >= COOLING_TEMPERATURE_CUTOFF_DEG10)
#endif
            ))
        {
            temperature_over_latched = RT_TRUE;
        }
        if (temperature_over_latched)
        {
            block_reason |= COOLING_PRESSURE_BLOCK_OVERTEMPERATURE;
        }
        dt_ms = pressure_ticks_to_ms(now - last_tick);
        last_tick = now;
#if COOLING_TEST_MODE
        if (!relief_service_control(&adc, block_reason, temperature_deg10,
                                    &targets.pressure_mpa1000))
            block_reason |= COOLING_PRESSURE_BLOCK_TEST_WAIT;
#endif
        duty_percent = pressure_update(next.raw, next.filtered,
                                       temperature_deg10, block_reason,
                                       dt_ms, &targets);

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
        next.warmup_complete = pressure_warmup_complete;
        next.target_pressure_mpa1000 = targets.pressure_mpa1000;
        next.target_temperature_deg10 = targets.temperature_deg10;
        next.block_reason = block_reason;

        rt_mutex_take(&pressure_lock, RT_WAITING_FOREVER);
        pressure_snapshot = next;
        rt_mutex_release(&pressure_lock);

#ifdef RT_USING_FINSH
        cooling_heater_debug_poll();

        if (!pressure_print_enabled)
        {
            last_print_tick = now;
        }
        else if (pressure_ticks_to_ms(now - last_print_tick) >=
                 COOLING_STATUS_PRINT_PERIOD_MS)
        {
            pressure_print_snapshot(&next);
            last_print_tick = now;
        }
#endif

        rt_thread_mdelay(COOLING_PRESSURE_CONTROL_PERIOD_MS);
    }
}

rt_err_t cooling_pressure_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    rt_mutex_init(&pressure_lock, "prslock", RT_IPC_FLAG_PRIO);
    rt_memset(&pressure_snapshot, 0, sizeof(pressure_snapshot));
    pressure_targets.pressure_mpa1000 = COOLING_PRESSURE_TARGET_MPA1000;
    pressure_targets.temperature_deg10 = COOLING_WARMUP_TARGET_TEMP_DEG10;
    pressure_targets.sequence = 0U;
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

#if COOLING_TEST_MODE
    return relief_service_init();
#else
    return RT_EOK;
#endif
}

void cooling_pressure_get_snapshot(cooling_pressure_snapshot_t *snapshot)
{
    rt_mutex_take(&pressure_lock, RT_WAITING_FOREVER);
    *snapshot = pressure_snapshot;
    rt_mutex_release(&pressure_lock);
}

rt_err_t cooling_pressure_set_targets(rt_int32_t pressure_mpa1000,
                                      rt_int32_t temperature_deg10)
{
#if COOLING_TEST_MODE
    /* 试验参数只允许使用 rc cfg 一次更新并由 rc start 激活。 */
    (void)pressure_mpa1000;
    (void)temperature_deg10;
    return -RT_EBUSY;
#else
    if ((pressure_mpa1000 <= 0) ||
        (pressure_mpa1000 > (rt_int32_t)COOLING_PRESSURE_FULL_MPA1000) ||
        (temperature_deg10 < -32768) || (temperature_deg10 > 32767))
    {
        return -RT_EINVAL;
    }
    if ((pressure_mpa1000 +
         (rt_int32_t)COOLING_PRESSURE_STOP_OFFSET_MPA1000 >=
         (rt_int32_t)COOLING_PRESSURE_CUTOFF_MPA1000) ||
        (temperature_deg10 < -400) ||
        (temperature_deg10 + COOLING_WARMUP_TEMP_HYSTERESIS_DEG10 >=
         COOLING_TEMPERATURE_CUTOFF_DEG10))
    {
        return -RT_EINVAL;
    }
    rt_mutex_take(&pressure_lock, RT_WAITING_FOREVER);
    pressure_targets.pressure_mpa1000 = (rt_uint16_t)pressure_mpa1000;
    pressure_targets.temperature_deg10 = (rt_int16_t)temperature_deg10;
    pressure_targets.sequence++;
    rt_mutex_release(&pressure_lock);
    return RT_EOK;
#endif
}

void cooling_pressure_get_targets(cooling_pressure_targets_t *targets)
{
    rt_mutex_take(&pressure_lock, RT_WAITING_FOREVER);
    *targets = pressure_targets;
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

static void pressure_print_snapshot(const cooling_pressure_snapshot_t *snapshot)
{
    rt_int32_t temperature = snapshot->temperature_deg10;
    rt_uint32_t absolute_temperature;
    rt_uint16_t pressure_mpa1000 = 0U;
    rt_bool_t pressure_valid = RT_FALSE;

    if (temperature < 0)
    {
        absolute_temperature = (rt_uint32_t)(-temperature);
    }
    else
    {
        absolute_temperature = (rt_uint32_t)temperature;
    }

    if (snapshot->adc_valid &&
        (cooling_sensor_pressure_mpa1000(snapshot->filtered,
                                         &pressure_mpa1000) == 0))
    {
        pressure_valid = RT_TRUE;
    }

    rt_kprintf("pressure seq=%u tick=%u state=%s duty=%u heater=%s "
               "tank=%s adc=%s temp=%s\n",
               (unsigned int)snapshot->sequence,
               (unsigned int)snapshot->tick,
               pressure_state_name[snapshot->state],
               (unsigned int)snapshot->duty_percent,
               snapshot->heater_on ? "on" : "off",
               snapshot->tank_present ? "present" : "absent",
               snapshot->adc_valid ? "valid" : "invalid",
               snapshot->temperature_valid ? "valid" : "invalid");
    rt_kprintf("  hysn2: raw=%u filtered=%u mv=%u",
               (unsigned int)snapshot->raw,
               (unsigned int)snapshot->filtered,
               (unsigned int)snapshot->millivolts);
    if (pressure_valid)
    {
        rt_kprintf(" pressure=%u.%03uMPa\n",
                   (unsigned int)(pressure_mpa1000 / 1000U),
                   (unsigned int)(pressure_mpa1000 % 1000U));
    }
    else
    {
        rt_kprintf(" pressure=invalid\n");
    }
    rt_kprintf("  target=%u.%03uMPa test=%u\n",
               (unsigned int)(snapshot->target_pressure_mpa1000 / 1000U),
               (unsigned int)(snapshot->target_pressure_mpa1000 % 1000U),
               (unsigned int)COOLING_TEST_MODE);
    temperature = snapshot->target_temperature_deg10;
    rt_kprintf("  ntc1: temp=%s%u.%uC target=%s%u.%uC warmup=%s\n",
               (snapshot->temperature_deg10 < 0) ? "-" : "",
               (unsigned int)(absolute_temperature / 10U),
               (unsigned int)(absolute_temperature % 10U),
               (temperature < 0) ? "-" : "",
               (unsigned int)((temperature < 0 ? -temperature : temperature) / 10),
               (unsigned int)((temperature < 0 ? -temperature : temperature) % 10),
               snapshot->warmup_complete ? "yes" : "no");
    rt_kprintf("  sufficient=%s low=%s overpressure=%s overtemperature=%s "
               "block=0x%08x\n",
               snapshot->refrigerant_sufficient ? "yes" : "no",
               snapshot->refrigerant_low ? "yes" : "no",
               (snapshot->block_reason &
                COOLING_PRESSURE_BLOCK_OVERPRESSURE) ? "yes" : "no",
               (snapshot->block_reason &
                COOLING_PRESSURE_BLOCK_OVERTEMPERATURE) ? "yes" : "no",
               (unsigned int)snapshot->block_reason);
}

/* 每次执行 pressure_status，反转压力状态的循环打印。 */
static void pressure_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    pressure_print_enabled = pressure_print_enabled ? RT_FALSE : RT_TRUE;
    rt_kprintf("pressure print %s\n",
               pressure_print_enabled ? "on" : "off");
}
MSH_CMD_EXPORT(pressure_status, toggle continuous pressure status printing);

/* 严格定点解析，拒绝尾随字符、过多小数位及溢出，不引入浮点格式库。 */
static rt_bool_t pressure_parse_decimal(const char *text, rt_uint8_t decimals,
                                        rt_int32_t *result)
{
    rt_int32_t value = 0;
    rt_uint8_t remaining = decimals;
    rt_bool_t negative = RT_FALSE;

    if ((*text == '-') || (*text == '+'))
    {
        negative = (*text == '-');
        text++;
    }
    if ((*text < '0') || (*text > '9'))
    {
        return RT_FALSE;
    }
    while ((*text >= '0') && (*text <= '9'))
    {
        value = value * 10 + (*text++ - '0');
        if (value > 32768)
        {
            return RT_FALSE;
        }
    }
    if (*text == '.')
    {
        text++;
        if ((*text < '0') || (*text > '9'))
        {
            return RT_FALSE;
        }
        while ((*text >= '0') && (*text <= '9') && (remaining > 0U))
        {
            value = value * 10 + (*text++ - '0');
            remaining--;
        }
    }
    while (remaining > 0U)
    {
        value *= 10;
        remaining--;
    }
    if ((*text != '\0') || (value > 32768))
    {
        return RT_FALSE;
    }
    *result = negative ? -value : value;
    return RT_TRUE;
}

static void heater_target(int argc, char **argv)
{
    cooling_pressure_targets_t targets;
    rt_int32_t pressure;
    rt_int32_t temperature;

    if (argc != 1)
    {
        if ((argc != 3) ||
            !pressure_parse_decimal(argv[1], 3U, &pressure) ||
            !pressure_parse_decimal(argv[2], 1U, &temperature))
        {
            rt_kprintf("usage: heater_target [pressure_MPa temperature_C]\n");
            rt_kprintf("example: heater_target 0.825 60.0\n");
            return;
        }
        if (cooling_pressure_set_targets(pressure, temperature) != RT_EOK)
        {
#if COOLING_TEST_MODE
            rt_kprintf("Qt relief test owns pressure targets; use rc cfg\n");
#else
            rt_kprintf("normal targets must stay below pressure/temp cutoffs\n");
#endif
            return;
        }
    }
    cooling_pressure_get_targets(&targets);
    temperature = targets.temperature_deg10;
    rt_kprintf("target=%u.%03uMPa temperature=%s%u.%uC test=%u\n",
               (unsigned int)(targets.pressure_mpa1000 / 1000U),
               (unsigned int)(targets.pressure_mpa1000 % 1000U),
               temperature < 0 ? "-" : "",
               (unsigned int)((temperature < 0 ? -temperature : temperature) / 10),
               (unsigned int)((temperature < 0 ? -temperature : temperature) % 10),
               (unsigned int)COOLING_TEST_MODE);
}
MSH_CMD_EXPORT(heater_target, get or set pressure MPa and temperature C targets);
#endif
