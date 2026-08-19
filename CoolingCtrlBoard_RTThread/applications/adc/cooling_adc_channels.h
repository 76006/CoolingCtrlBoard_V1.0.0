/*
 * CoolingCtrlBoard ADC 业务通道定义。
 *
 * MCU pins and ADC channel numbers remain in cooling_board_port.c/board.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_ADC_CHANNELS_H
#define APPLICATIONS_COOLING_ADC_CHANNELS_H

typedef enum
{
    COOLING_ADC_NTC0 = 0,
    COOLING_ADC_NTC1,
    COOLING_ADC_HYSN2
} cooling_adc_input_t;

#define COOLING_ADC_CHANNEL_COUNT       3U

#endif /* APPLICATIONS_COOLING_ADC_CHANNELS_H */
