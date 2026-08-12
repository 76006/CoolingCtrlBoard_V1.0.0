/*
 * CoolingCtrlBoard 业务通道定义
 *
 * 本文件只描述“采集什么”，不包含 MCU 引脚和 ADC 通道号。
 * 具体硬件映射由 cooling_board_port.c 完成。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_CHANNELS_H
#define APPLICATIONS_COOLING_CHANNELS_H

typedef enum
{
    COOLING_ADC_NTC0 = 0,  /* 第一路 NTC 温度采样 */
    COOLING_ADC_NTC1,      /* 第二路 NTC 温度采样 */
    COOLING_ADC_HYSN2      /* HYSN 模拟量采样 */
} cooling_adc_input_t;

#define COOLING_ADC_CHANNEL_COUNT       3U

#endif /* APPLICATIONS_COOLING_CHANNELS_H */
