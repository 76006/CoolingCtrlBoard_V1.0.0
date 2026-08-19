/*
 * CoolingCtrlBoard App 固件入口
 *
 * 负责把各层服务按安全顺序启动：
 *   1. 统一安全模块（默认失败关闭）；
 *   2. 发布 UID 授权结果到安全模块；
 *   3. 加热、罐体、ADC 模块；
 *   4. 启动压力控制；无效输入由状态机统一关断；
 *   5. 最后启动 CAN 业务与扩展帧 OTA。
 * 任何失败路径都不会误开加热；main() 只负责装配和启动
 * 结果记录，具体业务循环均位于独立服务线程。
 *
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <board.h>

#include "adc/cooling_adc.h"
#include "can/cooling_can.h"
#include "heater/cooling_heater.h"
#include "ota/cooling_ota.h"
#include "pressure/cooling_pressure.h"
#include "safety/cooling_safety.h"
#include "tank/cooling_tank.h"
#include "uid/cooling_uid_auth.h"

int main(void)
{
    rt_err_t adc_result;
    rt_err_t can_result;
    rt_err_t ota_result;
    rt_err_t pressure_result;
    rt_err_t tank_result;

    rt_kprintf("CoolingCtrlBoard / STM32F302CCT6 / %u Hz\n",
               (unsigned int)SystemCoreClock);
    /* 1. 统一安全模块：默认失败关闭。 */
    cooling_safety_init();

    /* 2. 发布 UID 授权结果（board 初始化阶段已完成密钥校验）。 */
    cooling_safety_set_authorized(is_encry_ok != 0U);

    /* 3. 各资源由自己的模块初始化。 */
    cooling_heater_start();

    tank_result = cooling_tank_start();
    adc_result = cooling_adc_start();

    /*
     * 4. 压力任务始终启动；ADC/罐体任务失败时对应快照保持无效，
     *    控制核心会保持加热关闭。
     */
    pressure_result = cooling_pressure_start();

    /* 5. CAN 总线先启动，OTA 依赖 CAN 接收线程。 */
    can_result = cooling_can_start();
    ota_result = can_result == RT_EOK ? cooling_ota_start() : -RT_ENOSYS;

    rt_kprintf("tank monitor: %s (%d)\n",
               tank_result == RT_EOK ? "started" : "failed",
               tank_result);
    rt_kprintf("adc sampler: %s (%d)\n",
               adc_result == RT_EOK ? "started" : "failed",
               adc_result);
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
