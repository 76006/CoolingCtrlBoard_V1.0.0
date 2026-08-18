/*
 * CoolingCtrlBoard App 固件入口
 *
 * 负责把各层服务按安全顺序启动：
 *   1. GPIO 最早保持加热低（先写低再设输出），并建立 LS 输入；
 *   2. 统一安全模块（默认失败关闭）；
 *   3. 发布 UID 授权结果到安全模块；
 *   4. 加热模块（先硬件拉低再建立状态）；
 *   5. 罐体/ADC/命令模块；
 *   6. 依赖全部成功后启动压力控制；
 *   7. 最后启动 CAN 业务与扩展帧 OTA。
 * 任何失败路径都不会误开加热；main() 只负责装配、依赖检查和启动
 * 结果记录，具体业务循环均位于独立服务线程。
 *
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "cooling_adc.h"
#include "cooling_board_port.h"
#include "can/cooling_can_service.h"
#include "cooling_command.h"
#include "cooling_ota.h"
#include "heater/cooling_heater.h"
#include "pressure/cooling_pressure.h"
#include "safety/cooling_safety.h"
#include "tank/cooling_tank.h"
#include "uid_encry/my_encry.h"

int main(void)
{
    rt_err_t adc_result;
    rt_err_t can_result;
    rt_err_t command_result;
    rt_err_t heater_result;
    rt_err_t ota_result;
    rt_err_t pressure_result;
    rt_err_t safety_result;
    rt_err_t tank_result;

    /*
     * 1. GPIO 最早保持加热低：板级适配层先写低再设置输出模式，
     *    保证应用服务启动前加热功率输出始终处于关闭状态。
     */
    cooling_board_heater_init();
    cooling_board_tank_input_init();

    rt_kprintf("CoolingCtrlBoard / STM32F302CCT6 / %u Hz\n",
               (unsigned int)SystemCoreClock);
    rt_kprintf("uart1: %s\n", rt_device_find("uart1") ? "ready" : "missing");
    rt_kprintf("adc1: %s\n", rt_device_find("adc1") ? "ready" : "missing");
    rt_kprintf("can1: %s\n", rt_device_find("can1") ? "ready" : "missing");
    rt_kprintf("limit switch: %d\n", cooling_board_tank_read());

    /* 2. 统一安全模块：默认失败关闭。 */
    safety_result = cooling_safety_init();

    /* 3. 发布 UID 授权结果（board 初始化阶段已完成密钥校验）。 */
    if (safety_result == RT_EOK)
    {
        (void)cooling_safety_set_authorized(is_encry_ok != 0U);
    }

    /* 4. 加热模块：先硬件拉低再建立状态。 */
    heater_result = cooling_heater_start();

    /* 5. 罐体监测、ADC 采样与命令状态。 */
    tank_result = cooling_tank_start();
    adc_result = cooling_adc_start();
    command_result = cooling_command_init();

    /*
     * 6. 压力控制依赖安全/加热/罐体/ADC/命令全部成功，任一依赖
     *    失败都不创建控制任务，此时加热保持上述关闭状态。
     */
    pressure_result = ((safety_result == RT_EOK) &&
                       (heater_result == RT_EOK) &&
                       (tank_result == RT_EOK) &&
                       (adc_result == RT_EOK) &&
                       (command_result == RT_EOK)) ?
        cooling_pressure_start() : -RT_ENOSYS;

    /* 7. CAN 总线先启动，OTA 依赖 CAN 接收线程。 */
    can_result = cooling_can_service_start();
    ota_result = can_result == RT_EOK ? cooling_ota_start() : -RT_ENOSYS;

    rt_kprintf("safety: %s (%d)\n",
               safety_result == RT_EOK ? "ready" : "failed",
               safety_result);
    rt_kprintf("heater: %s (%d)\n",
               heater_result == RT_EOK ? "started" : "failed",
               heater_result);
    rt_kprintf("tank monitor: %s (%d)\n",
               tank_result == RT_EOK ? "started" : "failed",
               tank_result);
    rt_kprintf("adc sampler: %s (%d)\n",
               adc_result == RT_EOK ? "started" : "failed",
               adc_result);
    rt_kprintf("command state: %s (%d)\n",
               command_result == RT_EOK ? "ready" : "failed",
               command_result);
    rt_kprintf("pressure control: %s (%d)\n",
               pressure_result == RT_EOK ? "started" : "failed",
               pressure_result);
    rt_kprintf("CAN bus: %s (%d)\n",
               can_result == RT_EOK ? "started" : "failed",
               can_result);
    rt_kprintf("CAN OTA: %s (%d)\n",
               ota_result == RT_EOK ? "started" : "failed",
               ota_result);

    return RT_EOK;
}
