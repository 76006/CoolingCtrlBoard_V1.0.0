/*
 * CoolingCtrlBoard 板级适配接口
 *
 * 上层任务只调用本文件中的接口，不直接依赖 RT-Thread ADC/GPIO 驱动。
 * 更换 MCU、RTOS、ADC 设备名或引脚时，主要修改本层和 board.h。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_BOARD_PORT_H
#define APPLICATIONS_COOLING_BOARD_PORT_H

#include <rtthread.h>

#include "cooling_channels.h"

/* 初始化/释放板上的 ADC 设备。ADC 由采样任务单线程使用。 */
rt_err_t cooling_board_adc_init(void);
void cooling_board_adc_deinit(void);

/* 读取指定业务通道的 12 位原始 ADC 值。 */
rt_err_t cooling_board_adc_read(cooling_adc_input_t input, rt_uint16_t *raw);

/* 初始化开关相关 GPIO，并保证加热输出默认为关闭。 */
void cooling_board_io_init(void);

/* 读取 PB15 当前电平；这里只返回电平，不判断常开/常闭语义。 */
rt_uint8_t cooling_board_limit_read(void);

/* 设置加热功率输出；安全联锁由应用控制层负责。 */
void cooling_board_heater_write(rt_bool_t enabled);

#endif /* APPLICATIONS_COOLING_BOARD_PORT_H */
