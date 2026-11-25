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

/*
 * 压力传感器：0.5--4.5 V 对应 0--1.600 MPa。
 * 板上调理电路理论增益为 72/157；ADC 参考电压为 3.0 V，
 * 因而零点约为 ADC 313，满量程约为 ADC 2817。
 */
#define COOLING_PRESSURE_CALIBRATED               1U
#define COOLING_PRESSURE_ADC_ZERO                  313U
#define COOLING_PRESSURE_ADC_FULL                 2817U
#define COOLING_PRESSURE_FULL_MPA1000             1600U

/* ---------------------------------------------------------------------
 * 冷媒压力加热控制配置（方案 A：GPIO 软件 PWM + 分阶段加热控制）
 * --------------------------------------------------------------------- */

/*
 * 1：Qt 泄压循环试验，上电停热，由串口显式启动，板端执行判定。
 * 0：正常模式，保留温压保护及升压超时。
 * 两种模式均保留 UID、标定、罐体、有效压力采样和 OTA 联锁。
 */
#ifndef COOLING_TEST_MODE
#define COOLING_TEST_MODE                       1
#endif
#if (COOLING_TEST_MODE != 0) && (COOLING_TEST_MODE != 1)
#error "COOLING_TEST_MODE must be 0 or 1"
#endif

/* 控制周期与软件 PWM 窗口。窗口必须是控制周期的整数倍。 */
#define COOLING_PRESSURE_CONTROL_PERIOD_MS      50U
#define COOLING_PRESSURE_PWM_WINDOW_MS          1000U

/* 正常模式预热温度默认值；运行时可用 heater_target 修改。 */
#define COOLING_WARMUP_TARGET_TEMP_DEG10          600
#define COOLING_WARMUP_TEMP_HYSTERESIS_DEG10       10
#define COOLING_WARMUP_DUTY_PERCENT              100U
#define COOLING_WARMUP_HOLD_DUTY_PERCENT          50U

/* NTC1 达到硬截止温度停热并锁存故障，拔罐后复位。 */
#define COOLING_TEMPERATURE_CUTOFF_DEG10           700

/* 目标压力默认值；运行时可用 heater_target 修改，分段阈值同步平移。 */
#define COOLING_PRESSURE_TARGET_MPA1000           1000U

/*
 * 相对目标压力配置分段间距，修改默认目标或串口目标均可生效。
 * 默认目标 0.825 MPa 时，快速/慢速分界为 0.750/0.820 MPa，
 * 回落到 0.800 MPa 补热；正常模式在 0.830 MPa 停热。
 * 测试模式直接在目标压力停热，不使用正常模式的停热偏移。
 */
#define COOLING_PRESSURE_FAST_MARGIN_MPA1000       75U
#define COOLING_PRESSURE_REHEAT_MARGIN_MPA1000     25U
#define COOLING_PRESSURE_SLOW_MARGIN_MPA1000        5U
#define COOLING_PRESSURE_STOP_OFFSET_MPA1000        5U
#define COOLING_PRESSURE_FAST_DUTY_PERCENT         30U
#define COOLING_PRESSURE_MEDIUM_DUTY_PERCENT       20U
#define COOLING_PRESSURE_SLOW_DUTY_PERCENT         10U

/* 压力达到硬截止值锁存故障。 */
#define COOLING_PRESSURE_CUTOFF_MPA1000           1400U
#define COOLING_PRESSURE_CUTOFF_ADC                \
    (COOLING_PRESSURE_ADC_ZERO +                                        \
     ((COOLING_PRESSURE_CUTOFF_MPA1000 *                                \
       (COOLING_PRESSURE_ADC_FULL - COOLING_PRESSURE_ADC_ZERO) +         \
       (COOLING_PRESSURE_FULL_MPA1000 / 2U)) /                             \
      COOLING_PRESSURE_FULL_MPA1000))

/* 正常模式：实际压力达到 1.100 MPa 才向主控报故障。 */
#define COOLING_PRESSURE_ERROR_REPORT_MPA1000     1300U

/* 正常模式：升压总时长超过该值仍未达标，锁存冷媒不足到拔罐。 */
#define COOLING_PRESSURE_MAX_BOOST_TIME_MS      300000U

/* ADC 快照超过该时间未更新视为陈旧，立即关闭加热。 */
#define COOLING_PRESSURE_ADC_MAX_AGE_MS         500U

/* ADC 合理范围检查：滤波值超出该区间视为传感器故障（占位）。 */
#define COOLING_PRESSURE_ADC_MIN_FILTERED       100U
#define COOLING_PRESSURE_ADC_MAX_FILTERED       4000U
/* 测试板传感器电气诊断，标定端点外留量程 2% 以容纳零点误差。 */
#define COOLING_TEST_PRESSURE_ADC_MARGIN        ((COOLING_PRESSURE_ADC_FULL - COOLING_PRESSURE_ADC_ZERO) / 50U)

/* 压力控制线程参数。优先级高于罐体监测(14)与 ADC(15)采样任务。 */
#if COOLING_TEST_MODE
#define COOLING_PRESSURE_THREAD_STACK_SIZE      1792U
#else
#define COOLING_PRESSURE_THREAD_STACK_SIZE      1024U
#endif
#define COOLING_PRESSURE_THREAD_PRIORITY        13U
#define COOLING_PRESSURE_THREAD_TIMESLICE       10U

#endif /* APPLICATIONS_COOLING_CONFIG_H */
