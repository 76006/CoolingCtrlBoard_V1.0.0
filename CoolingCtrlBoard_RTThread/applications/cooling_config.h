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

/* ADC 电气参数。当前毫伏值按 3.3 V 标称参考电压计算。 */
#define COOLING_ADC_REFERENCE_MV               3300U
#define COOLING_ADC_FULL_SCALE                 4095U

/* ADC 每 100 ms 采样一次；中值窗口必须为大于 1 的奇数。 */
#define COOLING_ADC_SAMPLE_PERIOD_MS           100U
#define COOLING_ADC_FILTER_WINDOW              5U

/* IIR: y += (x - y) / divisor，数值越大，输出越平滑。 */
#define COOLING_ADC_IIR_DIVISOR                4
#define COOLING_ADC_IIR_SCALE                  256

/* ADC 采样线程参数。RT-Thread 优先级数值越小，优先级越高。 */
#define COOLING_ADC_THREAD_STACK_SIZE          1024U
#define COOLING_ADC_THREAD_PRIORITY            15U
#define COOLING_ADC_THREAD_TIMESLICE           10U

/* 限位开关连续 5 次电平一致后，才确认状态发生变化。 */
#define COOLING_SWITCH_SAMPLE_PERIOD_MS        10U
#define COOLING_SWITCH_DEBOUNCE_SAMPLES        5U
#define COOLING_SWITCH_THREAD_STACK_SIZE       768U
#define COOLING_SWITCH_THREAD_PRIORITY         14U
#define COOLING_SWITCH_THREAD_TIMESLICE        10U

#if (COOLING_ADC_FILTER_WINDOW < 3U) || ((COOLING_ADC_FILTER_WINDOW % 2U) == 0U)
#error "COOLING_ADC_FILTER_WINDOW must be an odd number >= 3"
#endif

#if (COOLING_ADC_IIR_DIVISOR <= 0) || (COOLING_ADC_IIR_SCALE <= 0)
#error "ADC IIR parameters must be positive"
#endif

#if (COOLING_SWITCH_DEBOUNCE_SAMPLES == 0U)
#error "COOLING_SWITCH_DEBOUNCE_SAMPLES must be positive"
#endif

#endif /* APPLICATIONS_COOLING_CONFIG_H */
