/*
 * CoolingCtrlBoard ADC 采样服务
 *
 * 本层负责采样节拍、私有滤波算法和数据发布，不直接访问 ADC 驱动。
 * 硬件访问统一由 cooling_board_port.c 完成。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_adc.h"

#include "hardware/cooling_board_port.h"
#include "cooling_config.h"
#if COOLING_TEST_MODE
#include "heater/cooling_heater.h"
#endif

typedef struct
{
    rt_uint16_t sample[COOLING_ADC_FILTER_WINDOW];
    rt_uint8_t count;
    rt_uint8_t next;
    rt_int32_t iir_state;
    rt_bool_t iir_initialized;
} cooling_adc_filter_t;

/* 滤波器仅由 ADC 采样线程使用，不向其他模块暴露状态和算法接口。 */
static rt_uint16_t adc_filter_median(const cooling_adc_filter_t *filter)
{
    rt_uint16_t sorted[COOLING_ADC_FILTER_WINDOW];
    rt_uint8_t i;
    rt_uint8_t j;

    for (i = 0U; i < filter->count; i++)
    {
        sorted[i] = filter->sample[i];
    }

    for (i = 1U; i < filter->count; i++)
    {
        rt_uint16_t value = sorted[i];

        j = i;
        while ((j > 0U) && (sorted[j - 1U] > value))
        {
            sorted[j] = sorted[j - 1U];
            j--;
        }
        sorted[j] = value;
    }

    return sorted[filter->count / 2U];
}

static void adc_filter_init(cooling_adc_filter_t *filter)
{
    rt_uint8_t i;

    for (i = 0U; i < COOLING_ADC_FILTER_WINDOW; i++)
    {
        filter->sample[i] = 0U;
    }
    filter->count = 0U;
    filter->next = 0U;
    filter->iir_state = 0;
    filter->iir_initialized = RT_FALSE;
}

static rt_uint16_t adc_filter_update(cooling_adc_filter_t *filter,
                                     rt_uint16_t raw)
{
    rt_int32_t target;

    filter->sample[filter->next] = raw;
    filter->next = (rt_uint8_t)((filter->next + 1U) %
                                COOLING_ADC_FILTER_WINDOW);
    if (filter->count < COOLING_ADC_FILTER_WINDOW)
    {
        filter->count++;
    }

    target = (rt_int32_t)adc_filter_median(filter) * COOLING_ADC_IIR_SCALE;
    if (!filter->iir_initialized)
    {
        filter->iir_state = target;
        filter->iir_initialized = RT_TRUE;
    }
    else
    {
        filter->iir_state +=
            (target - filter->iir_state) / COOLING_ADC_IIR_DIVISOR;
    }

    return (rt_uint16_t)((filter->iir_state +
                          (COOLING_ADC_IIR_SCALE / 2)) /
                         COOLING_ADC_IIR_SCALE);
}

static const char *const adc_input_name[COOLING_ADC_CHANNEL_COUNT] =
{
    "ntc0",
    "ntc1",
    "hysn2"
};

/*
 * NTC0: TDK NTCG163JX103DT1S. The manufacturer table starts at -40 C.
 * Below it, estimate the trend from the two coldest nominal points:
 * B = ln(188500/144300)/(1/233.15 - 1/238.15) = 2967.28 K.
 * R(T) = 188500 * exp(B * (1/(T+273.15) - 1/233.15)).
 * The prefix is in 5 C steps; these are extrapolations, not calibration data.
 */
#define NTC0_OHM_AT_MINUS_40C 188500U
static const rt_uint32_t ntc0_resistance_ohm[] =
{
    9587903U, 6084784U, 3956056U, 2630029U, 1784869U,
    1234637U, 869286U, 622206U, 452234U, 333428U,
    249140U,
    NTC0_OHM_AT_MINUS_40C, 144300U, 111300U,  86600U,  67800U,
     53460U,  42450U,  33930U,  27280U,  22070U,
     17960U,  14700U,  12090U,  10000U,   8310U,
      6940U,   5826U,   4911U,   4158U,   3536U,
      3019U,   2588U,   2227U,   1924U,   1668U,
      1451U,   1267U,   1110U,    975U,    860U,
       760U,    674U,    599U,    534U,    478U,
       428U,    385U,    346U,    313U
};

/*
 * NTC1: ZY103I3950AI-1 (1%), manufacturer Rnom table.
 * -40 to 150 degrees C in 1 degree steps; values are resistance in ohms.
 */
static const rt_uint32_t ntc1_resistance_ohm[] =
{
    277225U, 263577U, 250070U, 236830U, 223952U, 211513U, 199566U, 188149U,
    177284U, 166983U, 157246U, 148067U, 139432U, 131325U, 123726U, 116610U,
    109955U, 103734U,  97923U,  92496U,  87429U,  82788U,  78439U,  74359U,
     70527U,  66925U,  63535U,  60343U,  57334U,  54495U,  51815U,  49283U,
     46890U,  44625U,  42481U,  40450U,  38526U,  36702U,  34971U,  33329U,
     31770U,  30253U,  28816U,  27453U,  26161U,  24935U,  23772U,  22668U,
     21621U,  20626U,  19682U,  18785U,  17933U,  17123U,  16353U,  15621U,
     14925U,  14264U,  13634U,  13035U,  12465U,  11923U,  11406U,  10915U,
     10446U,  10000U,   9575U,   9170U,   8784U,   8416U,   8064U,   7730U,
      7410U,   7106U,   6815U,   6538U,   6273U,   6020U,   5778U,   5548U,
      5327U,   5116U,   4915U,   4723U,   4539U,   4363U,   4195U,   4034U,
      3880U,   3733U,   3592U,   3457U,   3328U,   3204U,   3086U,   2972U,
      2863U,   2759U,   2659U,   2564U,   2472U,   2384U,   2299U,   2218U,
      2140U,   2066U,   1994U,   1926U,   1860U,   1796U,   1735U,   1677U,
      1621U,   1567U,   1515U,   1465U,   1417U,   1371U,   1326U,   1284U,
      1242U,   1203U,   1165U,   1128U,   1093U,   1059U,   1027U,    996U,
       965U,    936U,    908U,    881U,    855U,    830U,    805U,    782U,
       759U,    737U,    715U,    694U,    674U,    656U,    638U,    620U,
       603U,    586U,    569U,    554U,    538U,    523U,    508U,    494U,
       480U,    467U,    454U,    441U,    429U,    417U,    406U,    394U,
       384U,    373U,    363U,    353U,    343U,    334U,    325U,    317U,
       308U,    300U,    292U,    285U,    277U,    270U,    263U,    256U,
       250U,    244U,    238U,    232U,    226U,    220U,    215U,    210U,
       204U,    199U,    195U,    190U,    186U,    181U,    177U
};

#define NTC0_TABLE_FIRST_TEMP_DEG10       (-950)
#define NTC1_TABLE_FIRST_TEMP_DEG10       (-400)
#define NTC0_CAL_MIN_DEG10                (-400)
#define NTC0_TABLE_STEP_DEG10               50
#define NTC1_TABLE_STEP_DEG10               10
#define NTC0_TABLE_COUNT                   \
    (sizeof(ntc0_resistance_ohm) / sizeof(ntc0_resistance_ohm[0]))
#define NTC1_TABLE_COUNT                   \
    (sizeof(ntc1_resistance_ohm) / sizeof(ntc1_resistance_ohm[0]))

static struct rt_mutex adc_snapshot_lock;
static cooling_adc_snapshot_t adc_snapshot;

#ifdef RT_USING_FINSH
static volatile rt_uint8_t adc_print_mask = 0U;

static void adc_print_snapshot(const cooling_adc_snapshot_t *snapshot)
{
    rt_uint8_t print_mask = adc_print_mask;
    rt_uint8_t input;

    rt_kprintf("adc seq=%u tick=%u valid=0x%02x\n",
               (unsigned int)snapshot->sequence,
               (unsigned int)snapshot->tick,
               (unsigned int)snapshot->valid_mask);
    for (input = 0U; input < COOLING_ADC_CHANNEL_COUNT; input++)
    {
        rt_bool_t valid =
            (snapshot->valid_mask & COOLING_ADC_VALID_BIT(input)) != 0U;

        if ((print_mask & COOLING_ADC_VALID_BIT(input)) == 0U)
        {
            continue;
        }

        rt_kprintf("  %s: raw=%u filtered=%u mv=%u %s",
                   adc_input_name[input],
                   (unsigned int)snapshot->value[input].raw,
                   (unsigned int)snapshot->value[input].filtered,
                   (unsigned int)snapshot->value[input].millivolts,
                   valid ? "valid" : "invalid");

        if (valid && (input <= COOLING_ADC_NTC1))
        {
            rt_int16_t temperature_deg10;

            if (cooling_ntc_from_adc(
                    (cooling_adc_input_t)input,
                    snapshot->value[input].filtered,
                    &temperature_deg10) == 0)
            {
                rt_int32_t absolute_temperature = temperature_deg10;

                if (absolute_temperature < 0)
                {
                    absolute_temperature = -absolute_temperature;
                }
                rt_kprintf(" temp=%s%d.%dC%s",
                           (temperature_deg10 < 0) ? "-" : "",
                           (int)(absolute_temperature / 10),
                           (int)(absolute_temperature % 10),
                           (input == COOLING_ADC_NTC0 && temperature_deg10 < NTC0_CAL_MIN_DEG10) ?
                           " (estimated)" : "");
            }
            else
            {
                rt_kprintf(" temp=invalid");
            }
        }
        else if (valid && (input == COOLING_ADC_HYSN2))
        {
            rt_uint16_t pressure_mpa1000;

            rt_kprintf(" sensor_mv=%u", (unsigned int)cooling_pressure_sensor_mv(
                snapshot->value[input].filtered));
            if (cooling_sensor_pressure_mpa1000(
                    snapshot->value[input].filtered,
                    &pressure_mpa1000) == 0)
            {
                rt_kprintf(" pressure=%u.%03uMPa",
                           (unsigned int)(pressure_mpa1000 / 1000U),
                           (unsigned int)(pressure_mpa1000 % 1000U));
            }
            else
            {
                rt_kprintf(" pressure=invalid");
            }
        }
        rt_kprintf("\n");
    }
}
#endif

/*
 * ADC 任务是硬件 ADC 的唯一使用者。
 * 每轮完成三路采样后再整体发布快照，CAN 线程不会读到半更新的数据。
 */
static void adc_sample_entry(void *parameter)
{
    cooling_adc_filter_t filter[COOLING_ADC_CHANNEL_COUNT];
    cooling_adc_snapshot_t next_snapshot = {0};
    rt_uint8_t previous_valid_mask = 0xFFU;
    rt_uint8_t input;

    (void)parameter;

    for (input = 0U; input < COOLING_ADC_CHANNEL_COUNT; input++)
    {
        adc_filter_init(&filter[input]);
    }

    while (1)
    {
        next_snapshot.valid_mask = 0U;

        for (input = 0U; input < COOLING_ADC_CHANNEL_COUNT; input++)
        {
            rt_uint16_t raw;

            if (cooling_board_adc_read((cooling_adc_input_t)input, &raw) == RT_EOK)
            {
                rt_uint16_t filtered =
                    adc_filter_update(&filter[input], raw);

                next_snapshot.value[input].raw = raw;
                next_snapshot.value[input].filtered = filtered;
                next_snapshot.value[input].millivolts =
                    (rt_uint16_t)(((rt_uint32_t)filtered *
                                   COOLING_ADC_REFERENCE_MV +
                                   (COOLING_ADC_FULL_SCALE / 2U)) /
                                  COOLING_ADC_FULL_SCALE);
                next_snapshot.valid_mask |= COOLING_ADC_VALID_BIT(input);
            }
        }

#if COOLING_TEST_MODE
        {
            cooling_heater_snapshot_t heater;
            cooling_heater_get_snapshot(&heater);
            next_snapshot.heater_on = heater.heater_on != RT_FALSE;
        }
#endif
        next_snapshot.sequence++;
        next_snapshot.tick = rt_tick_get();

        /* 互斥量只保护一次结构体复制，锁定时间很短。 */
        rt_mutex_take(&adc_snapshot_lock, RT_WAITING_FOREVER);
        adc_snapshot = next_snapshot;
        rt_mutex_release(&adc_snapshot_lock);

        /* 有效状态发生变化时才打印，避免硬件故障时刷屏。 */
        if ((next_snapshot.valid_mask != previous_valid_mask) &&
            (next_snapshot.valid_mask != COOLING_ADC_VALID_ALL))
        {
            rt_kprintf("adc sampler: valid mask 0x%02x\n",
                       (unsigned int)next_snapshot.valid_mask);
        }
        previous_valid_mask = next_snapshot.valid_mask;

#ifdef RT_USING_FINSH
        if (adc_print_mask != 0U)
        {
            adc_print_snapshot(&next_snapshot);
        }
#endif

        rt_thread_mdelay(COOLING_ADC_SAMPLE_PERIOD_MS);
    }
}

rt_err_t cooling_adc_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    rt_mutex_init(&adc_snapshot_lock, "adclock", RT_IPC_FLAG_PRIO);
    rt_memset(&adc_snapshot, 0, sizeof(adc_snapshot));

    result = cooling_board_adc_init();
    if (result != RT_EOK)
    {
        return result;
    }

    thread = rt_thread_create("adcflt", adc_sample_entry, RT_NULL,
                              COOLING_ADC_THREAD_STACK_SIZE,
                              COOLING_ADC_THREAD_PRIORITY,
                              COOLING_ADC_THREAD_TIMESLICE);
    if (thread == RT_NULL)
    {
        cooling_board_adc_deinit();
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(thread);
        cooling_board_adc_deinit();
        return result;
    }

    return RT_EOK;
}

void cooling_adc_get_snapshot(cooling_adc_snapshot_t *snapshot)
{
    rt_mutex_take(&adc_snapshot_lock, RT_WAITING_FOREVER);
    *snapshot = adc_snapshot;
    rt_mutex_release(&adc_snapshot_lock);
}

static int ntc_temperature_from_ohms(rt_uint32_t resistance,
                                     const rt_uint32_t *resistance_table,
                                     rt_size_t table_count,
                                     rt_int16_t first_temp_deg10,
                                     rt_uint16_t table_step_deg10,
                                     rt_int16_t *temperature_deg10)
{
    rt_uint32_t index;
    rt_int32_t temperature;

    if ((resistance > resistance_table[0]) ||
        (resistance < resistance_table[table_count - 1U]))
    {
        return -1;
    }

    if (resistance == resistance_table[table_count - 1U])
    {
        *temperature_deg10 =
            (rt_int16_t)(first_temp_deg10 +
                         (rt_int32_t)((table_count - 1U) *
                                      table_step_deg10));
        return 0;
    }

    for (index = 0U; index < (table_count - 1U); index++)
    {
        rt_uint32_t upper_resistance = resistance_table[index];
        rt_uint32_t lower_resistance = resistance_table[index + 1U];

        if ((resistance <= upper_resistance) &&
            (resistance >= lower_resistance))
        {
            rt_uint32_t resistance_span =
                upper_resistance - lower_resistance;
            rt_uint32_t temperature_offset =
                ((upper_resistance - resistance) *
                 table_step_deg10 +
                 (resistance_span / 2U)) / resistance_span;

            temperature = first_temp_deg10 +
                          (rt_int32_t)(index *
                                       table_step_deg10) +
                          (rt_int32_t)temperature_offset;
            *temperature_deg10 = (rt_int16_t)temperature;
            return 0;
        }
    }

    return -1;
}

int cooling_ntc_from_adc(cooling_adc_input_t input, rt_uint16_t adc,
                         rt_int16_t *temperature_deg10)
{
    rt_uint32_t adc_mv_scaled = (rt_uint32_t)adc * COOLING_ADC_REFERENCE_MV;
    rt_uint32_t voltage_mv = (adc_mv_scaled + COOLING_ADC_FULL_SCALE / 2U) / COOLING_ADC_FULL_SCALE;
    rt_uint32_t denominator;
    rt_uint32_t resistance;

    if (voltage_mv >= COOLING_NTC_SUPPLY_MV)
        return -1;
    denominator = COOLING_NTC_SUPPLY_MV - voltage_mv;
    resistance = (COOLING_NTC_PULLUP_OHM * voltage_mv + denominator / 2U) / denominator;
    if (input == COOLING_ADC_NTC0)
    {
        if (adc >= COOLING_NTC0_OPEN_ADC)
            return -1;
        if (resistance > NTC0_OHM_AT_MINUS_40C)
        {
            /* Near the supply rail, rounding to whole millivolts loses ADC codes. */
            denominator = COOLING_NTC_SUPPLY_MV * COOLING_ADC_FULL_SCALE - adc_mv_scaled;
            resistance = (rt_uint32_t)(((uint64_t)COOLING_NTC_PULLUP_OHM * adc_mv_scaled +
                                        denominator / 2U) / denominator);
            if (resistance < NTC0_OHM_AT_MINUS_40C)
                resistance = NTC0_OHM_AT_MINUS_40C;
        }
        return ntc_temperature_from_ohms(resistance, ntc0_resistance_ohm, NTC0_TABLE_COUNT,
                                         NTC0_TABLE_FIRST_TEMP_DEG10, NTC0_TABLE_STEP_DEG10,
                                         temperature_deg10);
    }
    if (input == COOLING_ADC_NTC1)
        return ntc_temperature_from_ohms(resistance, ntc1_resistance_ohm, NTC1_TABLE_COUNT,
                                         NTC1_TABLE_FIRST_TEMP_DEG10, NTC1_TABLE_STEP_DEG10,
                                         temperature_deg10);
    return -1;
}

uint16_t cooling_pressure_sensor_mv(uint16_t adc)
{
    /* Vadc / 0.4586，保留 ADC 换算精度直到传感器毫伏值取整。 */
    uint64_t numerator = (uint64_t)adc * COOLING_ADC_REFERENCE_MV *
                         COOLING_PRESSURE_GAIN_DEN;
    uint32_t denominator = COOLING_ADC_FULL_SCALE * COOLING_PRESSURE_GAIN_NUM;

    return (uint16_t)((numerator + denominator / 2U) / denominator);
}

typedef struct
{
    uint16_t sensor_mv;
    uint16_t mpa1000;
} pressure_cal_point_t;

static const pressure_cal_point_t pressure_cal[] = { COOLING_PRESSURE_CAL_POINTS };
#define PRESSURE_CAL_COUNT (sizeof(pressure_cal) / sizeof(pressure_cal[0]))

uint16_t cooling_pressure_adc_from_mpa1000(uint16_t pressure_mpa1000)
{
    size_t hi = 1;
    while (hi + 1 < PRESSURE_CAL_COUNT && pressure_mpa1000 > pressure_cal[hi].mpa1000)
    {
        ++hi;
    }
    const pressure_cal_point_t *low = &pressure_cal[hi - 1];
    const pressure_cal_point_t *high = &pressure_cal[hi];
    uint32_t span_pressure = high->mpa1000 - low->mpa1000;
    uint32_t span_mv = high->sensor_mv - low->sensor_mv;
    /* 保留插值的小数，仅在得到最终 ADC 码值时取整。 */
    uint32_t scaled_mv = low->sensor_mv * span_pressure +
                         (pressure_mpa1000 - low->mpa1000) * span_mv;
    uint64_t numerator = (uint64_t)scaled_mv * COOLING_PRESSURE_GAIN_NUM * COOLING_ADC_FULL_SCALE;
    uint64_t denominator = (uint64_t)span_pressure * COOLING_PRESSURE_GAIN_DEN * COOLING_ADC_REFERENCE_MV;
    return (uint16_t)((numerator + denominator / 2U) / denominator);
}

int cooling_sensor_pressure_mpa1000(uint16_t filtered_adc,
                                    uint16_t *pressure_mpa1000)
{
#if COOLING_PRESSURE_CALIBRATED != 0
    uint16_t sensor_mv = cooling_pressure_sensor_mv(filtered_adc);
    size_t hi = 1;
    if (sensor_mv <= pressure_cal[0].sensor_mv)
    {
        *pressure_mpa1000 = 0U;
        return 0;
    }
    while (hi + 1 < PRESSURE_CAL_COUNT && sensor_mv > pressure_cal[hi].sensor_mv)
    {
        ++hi;
    }
    const pressure_cal_point_t *low = &pressure_cal[hi - 1];
    const pressure_cal_point_t *high = &pressure_cal[hi];
    uint32_t span_mv = high->sensor_mv - low->sensor_mv;
    uint32_t span_pressure = high->mpa1000 - low->mpa1000;
    uint32_t pressure = low->mpa1000 +
        ((sensor_mv - low->sensor_mv) * span_pressure + span_mv / 2U) / span_mv;
    if (pressure > COOLING_PRESSURE_FULL_MPA1000)
    {
        pressure = COOLING_PRESSURE_FULL_MPA1000;
    }
    *pressure_mpa1000 = (uint16_t)pressure;
    return 0;
#else
    (void)filtered_adc;
    *pressure_mpa1000 = 0U;
    return -1;
#endif
}

int cooling_sensor_ntc1_temperature_deg10(int16_t *temperature_deg10)
{
    cooling_adc_snapshot_t adc;

    *temperature_deg10 = 0;
    cooling_adc_get_snapshot(&adc);
    if ((adc.valid_mask & COOLING_ADC_VALID_BIT(COOLING_ADC_NTC1)) == 0U)
    {
        return -1;
    }

    return cooling_ntc_from_adc(
        COOLING_ADC_NTC1,
        adc.value[COOLING_ADC_NTC1].filtered,
        temperature_deg10);
}

int cooling_sensor_temperature_deg10(uint16_t *temperature_deg10)
{
    rt_int16_t signed_temperature;

    *temperature_deg10 = 0U;
    if ((cooling_sensor_ntc1_temperature_deg10(&signed_temperature) != 0) ||
        (signed_temperature < 0))
    {
        return -1;
    }

    *temperature_deg10 = (rt_uint16_t)signed_temperature;
    return 0;
}

#ifdef RT_USING_FINSH
static void adc_toggle_print(cooling_adc_input_t input)
{
    rt_uint8_t bit = COOLING_ADC_VALID_BIT(input);

    adc_print_mask ^= bit;
    rt_kprintf("%s print %s\n", adc_input_name[input],
               (adc_print_mask & bit) ? "on" : "off");
}

/* 每次执行 adc_values，同时开启或关闭三路 ADC 循环打印。 */
static void adc_values(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    adc_print_mask = (adc_print_mask == COOLING_ADC_VALID_ALL) ?
        0U : COOLING_ADC_VALID_ALL;
    rt_kprintf("all adc print %s\n", adc_print_mask ? "on" : "off");
}
MSH_CMD_EXPORT(adc_values, toggle continuous CoolingCtrlBoard ADC printing);

static void ntc0_values(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    adc_toggle_print(COOLING_ADC_NTC0);
}
MSH_CMD_EXPORT(ntc0_values, toggle continuous NTC0 printing);

static void ntc1_values(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    adc_toggle_print(COOLING_ADC_NTC1);
}
MSH_CMD_EXPORT(ntc1_values, toggle continuous NTC1 printing);

static void pressure_values(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    adc_toggle_print(COOLING_ADC_HYSN2);
}
MSH_CMD_EXPORT(pressure_values, toggle continuous pressure ADC printing);
#endif
