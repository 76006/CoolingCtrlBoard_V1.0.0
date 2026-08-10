/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_gpio.h>

int main(void)
{
    rt_pin_mode(BOARD_HEATER_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(BOARD_HEATER_POWER_PIN, PIN_LOW);
    rt_pin_mode(BOARD_LIMIT_SWITCH_PIN, PIN_MODE_INPUT);

    rt_kprintf("CoolingCtrlBoard / STM32F302CCT6 / %u Hz\n",
               (unsigned int)SystemCoreClock);
    rt_kprintf("uart1: %s\n", rt_device_find("uart1") ? "ready" : "missing");
    rt_kprintf("adc1: %s\n", rt_device_find("adc1") ? "ready" : "missing");
    rt_kprintf("can1: %s\n", rt_device_find("can1") ? "ready" : "missing");
    rt_kprintf("limit switch: %d\n", rt_pin_read(BOARD_LIMIT_SWITCH_PIN));

    return RT_EOK;
}
