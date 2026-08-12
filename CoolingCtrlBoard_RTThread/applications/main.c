/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_gpio.h>

#include "cooling_adc.h"
#include "cooling_switch.h"

int main(void)
{
    rt_err_t adc_result;
    rt_err_t switch_result;

    /*
     * 板级初始化阶段已经将 PB0 安全拉低；这里再次明确设置，
     * 保证应用服务启动前加热功率输出始终处于关闭状态。
     */
    rt_pin_mode(BOARD_HEATER_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(BOARD_HEATER_POWER_PIN, PIN_LOW);
    rt_pin_mode(BOARD_LIMIT_SWITCH_PIN, PIN_MODE_INPUT);

    rt_kprintf("CoolingCtrlBoard / STM32F302CCT6 / %u Hz\n",
               (unsigned int)SystemCoreClock);
    rt_kprintf("uart1: %s\n", rt_device_find("uart1") ? "ready" : "missing");
    rt_kprintf("adc1: %s\n", rt_device_find("adc1") ? "ready" : "missing");
    rt_kprintf("can1: %s\n", rt_device_find("can1") ? "ready" : "missing");
    rt_kprintf("limit switch: %d\n", rt_pin_read(BOARD_LIMIT_SWITCH_PIN));

    /* 业务服务分别负责开关消抖和 ADC 采样，main 不承载循环任务。 */
    switch_result = cooling_switch_start();
    adc_result = cooling_adc_start();
    rt_kprintf("switch monitor: %s (%d)\n",
               switch_result == RT_EOK ? "started" : "failed",
               switch_result);
    rt_kprintf("adc sampler: %s (%d)\n",
               adc_result == RT_EOK ? "started" : "failed",
               adc_result);

    return RT_EOK;
}
