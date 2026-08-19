/*
 * CoolingCtrlBoard 板级适配接口
 *
 * 上层任务只调用本文件中的接口，不直接依赖 RT-Thread ADC/GPIO 驱动。
 * 更换 MCU、RTOS、ADC 设备名或引脚时，主要修改本层和 board.h。
 *
 * 加热输出与 LS 输入分属两个模块，因此初始化按资源拆分：
 * cooling_board_heater_init() 只归加热模块调用，
 * cooling_board_tank_input_init()/cooling_board_tank_read() 只归
 * 罐体模块调用。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_BOARD_PORT_H
#define APPLICATIONS_COOLING_BOARD_PORT_H

#include <rtthread.h>

#include "adc/cooling_adc_channels.h"

/* 初始化/释放板上的 ADC 设备。ADC 由采样任务单线程使用。 */
rt_err_t cooling_board_adc_init(void);
void cooling_board_adc_deinit(void);

/* 读取指定业务通道的 12 位原始 ADC 值。 */
rt_err_t cooling_board_adc_read(cooling_adc_input_t input, rt_uint16_t *raw);

/* 初始化加热输出 GPIO：先写低再设为输出，避免初始化瞬间误开启加热。 */
void cooling_board_heater_init(void);

/* 初始化 LS 输入 GPIO 为输入模式。 */
void cooling_board_tank_input_init(void);

/* 读取 PB15 当前电平；这里只返回电平，不判断常开/常闭语义。 */
rt_uint8_t cooling_board_tank_read(void);

/* 设置加热功率输出；安全联锁由加热模块负责。 */
void cooling_board_heater_write(rt_bool_t enabled);

#endif /* APPLICATIONS_COOLING_BOARD_PORT_H */
