/*
 * CoolingCtrlBoard 应用配置
 *
 * 所有与采样周期、滤波强度和线程资源有关的参数集中在这里，
 * 移植到其他板卡或调整控制节拍时，不需要修改业务逻辑。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_CONFIG_H
#define APPLICATIONS_COOLING_CONFIG_H

#include "adc/cooling_adc_channels.h"

/* MCU 的 VDDA/VREF+ 接 VREF_3V，ADC 毫伏值按 3.0 V 标称值计算。 */
#define COOLING_ADC_REFERENCE_MV               3000U
#define COOLING_ADC_FULL_SCALE                 4095U

/* ADC 每 100 ms 采样一次；中值窗口必须为大于 1 的奇数。 */
#define COOLING_ADC_SAMPLE_PERIOD_MS           100U
#define COOLING_ADC_FILTER_WINDOW              5U

/* IIR: y += (x - y) / divisor，数值越大，输出越平滑。 */
#define COOLING_ADC_IIR_DIVISOR                4
#define COOLING_ADC_IIR_SCALE                  256

/* pressure_status、tank_status、heater_status 的循环打印周期。 */
#define COOLING_STATUS_PRINT_PERIOD_MS          500U

/* ADC 采样线程参数。RT-Thread 优先级数值越小，优先级越高。 */
#define COOLING_ADC_THREAD_STACK_SIZE          1024U
#define COOLING_ADC_THREAD_PRIORITY            15U
#define COOLING_ADC_THREAD_TIMESLICE           10U

/* 罐体限位开关连续 5 次电平一致后，才确认状态发生变化。 */
#define COOLING_TANK_SAMPLE_PERIOD_MS          10U
#define COOLING_TANK_DEBOUNCE_SAMPLES          5U
#define COOLING_TANK_THREAD_STACK_SIZE         768U
#define COOLING_TANK_THREAD_PRIORITY           14U
#define COOLING_TANK_THREAD_TIMESLICE          10U

/* NTC0/NTC1 均为 VREF_3V -- 10k 上拉 -- ADC -- NTC -- GND。 */
#define COOLING_NTC_SUPPLY_MV                     3000U
#define COOLING_NTC_PULLUP_OHM                    10000U

/* PM13V 理论换算：0 MPa 对应 ADC 0，2.000 MPa 对应 ADC 3131。 */
#define COOLING_PRESSURE_CALIBRATED               1U
#define COOLING_PRESSURE_ADC_FULL                 3131U
#define COOLING_PRESSURE_FULL_MPA1000             2000U

/* ---------------------------------------------------------------------
 * 冷媒压力加热控制配置（方案 A：GPIO 软件 PWM + 分阶段加热控制）
 * --------------------------------------------------------------------- */

/* 控制周期与软件 PWM 窗口。窗口必须是控制周期的整数倍。 */
#define COOLING_PRESSURE_CONTROL_PERIOD_MS      50U
#define COOLING_PRESSURE_PWM_WINDOW_MS          1000U

/* 温度单位为 0.1 摄氏度；滞回 10 表示目标上下各 1.0 摄氏度。 */
#define COOLING_WARMUP_TARGET_TEMP_DEG10          600
#define COOLING_WARMUP_TEMP_HYSTERESIS_DEG10       10
#define COOLING_HOLD_TARGET_TEMP_DEG10            400
#define COOLING_HOLD_TEMP_HYSTERESIS_DEG10         10

/* 升到目标温度使用满功率，升温目标/40 摄氏度保温使用低功率软件 PWM。 */
#define COOLING_WARMUP_DUTY_PERCENT              100U
#define COOLING_WARMUP_HOLD_DUTY_PERCENT          50U
#define COOLING_FINAL_HOLD_DUTY_PERCENT           35U

/* 压力达到 0.650 MPa 后，从升温目标切换到 40 摄氏度保温。 */
#define COOLING_PRESSURE_TARGET_MPA1000           650U
#define COOLING_PRESSURE_TARGET_ADC                \
    ((COOLING_PRESSURE_TARGET_MPA1000 * COOLING_PRESSURE_ADC_FULL +       \
      (COOLING_PRESSURE_FULL_MPA1000 / 2U)) /                             \
     COOLING_PRESSURE_FULL_MPA1000)

/* 压力连续达到目标并保持该时间后，才确认“冷媒充足”。 */
#define COOLING_PRESSURE_CONFIRM_TIME_MS        1000U

/* 升压总时长超过该值仍未达标，判定冷媒不足并锁存到拔罐。 */
#define COOLING_PRESSURE_MAX_BOOST_TIME_MS      300000U

/* ADC 快照超过该时间未更新视为陈旧，立即关闭加热。 */
#define COOLING_PRESSURE_ADC_MAX_AGE_MS         500U

/* ADC 合理范围检查：滤波值超出该区间视为传感器故障（占位）。 */
#define COOLING_PRESSURE_ADC_MIN_FILTERED       100U
#define COOLING_PRESSURE_ADC_MAX_FILTERED       4000U

/* 压力控制线程参数。优先级高于罐体监测(14)与 ADC(15)采样任务。 */
#define COOLING_PRESSURE_THREAD_STACK_SIZE      1024U
#define COOLING_PRESSURE_THREAD_PRIORITY        13U
#define COOLING_PRESSURE_THREAD_TIMESLICE       10U

#endif /* APPLICATIONS_COOLING_CONFIG_H */
