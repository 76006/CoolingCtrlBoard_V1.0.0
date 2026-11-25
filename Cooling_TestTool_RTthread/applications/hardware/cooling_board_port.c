/*
 * CoolingCtrlBoard RT-Thread/STM32 硬件适配实现
 *
 * 负责业务通道到 STM32 ADC 通道的映射、ADC 设备访问、LS 输入读取和
 * 加热 GPIO 输出。应用层不得绕过本文件直接依赖具体 MCU 驱动，移植
 * 到其他芯片或修改引脚时主要调整本文件及 board.h。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_board_port.h"

#include <board.h>
#include <drv_gpio.h>
#include <rtdevice.h>

#include "cooling_config.h"

#define BOARD_ADC_DEVICE_NAME         "adc1"

/* 业务通道到 STM32 ADC 通道的唯一映射点。 */
static const rt_int8_t board_adc_channel[COOLING_ADC_CHANNEL_COUNT] =
{
    BOARD_NTC0_ADC_CHANNEL,
    BOARD_NTC1_ADC_CHANNEL,
    BOARD_HYSN_ADC_CHANNEL
};

static rt_adc_device_t board_adc_device;

rt_err_t cooling_board_adc_init(void)
{
    board_adc_device =
        (rt_adc_device_t)rt_device_find(BOARD_ADC_DEVICE_NAME);
    if (board_adc_device == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    return rt_device_open((rt_device_t)board_adc_device,
                          RT_DEVICE_OFLAG_RDONLY);
}

void cooling_board_adc_deinit(void)
{
    rt_device_close((rt_device_t)board_adc_device);
    board_adc_device = RT_NULL;
}

rt_err_t cooling_board_adc_read(cooling_adc_input_t input, rt_uint16_t *raw)
{
    rt_uint32_t sample = 0U;
    rt_uint32_t input_index = (rt_uint32_t)input;
    rt_ssize_t read_count;
    rt_err_t result;
    rt_err_t stop_result;

    /* STM32 当前配置为单通道转换，因此每次读取前重新选择通道。 */
    result = rt_adc_enable(board_adc_device, board_adc_channel[input_index]);
    if (result != RT_EOK)
    {
        return result;
    }

    read_count = rt_device_read((rt_device_t)board_adc_device,
                                board_adc_channel[input_index],
                                &sample,
                                1U);
    stop_result = rt_adc_disable(board_adc_device,
                                 board_adc_channel[input_index]);

    if (read_count != 1)
    {
        return -RT_ERROR;
    }
    if (stop_result != RT_EOK)
    {
        return stop_result;
    }

    *raw = (rt_uint16_t)sample;

    return RT_EOK;
}

void cooling_board_heater_init(void)
{
    /* 先写低再设置输出模式，避免初始化瞬间误开启加热功率级。 */
    rt_pin_write(BOARD_HEATER_POWER_PIN, PIN_LOW);
    rt_pin_mode(BOARD_HEATER_POWER_PIN, PIN_MODE_OUTPUT);
}

void cooling_board_tank_input_init(void)
{
    rt_pin_mode(BOARD_LIMIT_SWITCH_PIN, PIN_MODE_INPUT);
}

rt_uint8_t cooling_board_tank_read(void)
{
    return (rt_uint8_t)rt_pin_read(BOARD_LIMIT_SWITCH_PIN);
}

void cooling_board_heater_write(rt_bool_t enabled)
{
    rt_pin_write(BOARD_HEATER_POWER_PIN, enabled ? PIN_HIGH : PIN_LOW);
}
