/*
 * CoolingCtrlBoard 传感器物理量模块公共接口
 *
 * 负责把滤波后的 ADC 数据换算成系统统一的压力和温度定点值，并封装
 * 温度通道选择与 ADC 快照读取。CAN、显示或日志等使用者只获取物理量，
 * 不需要知道具体 ADC 通道、标定参数和换算公式。
 *
 * 本模块不创建线程、不操作 GPIO，也不增加互斥量；ADC 快照的线程
 * 安全仍由 cooling_adc 模块负责。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_SENSOR_H
#define APPLICATIONS_COOLING_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 压力换算：filtered_adc（0..4095）-> 0.001 MPa 定点值。
 * 未标定或参数无效时返回 -1，并把 *pressure_mpa1000 写为 0。
 */
int cooling_sensor_pressure_mpa1000(uint16_t filtered_adc,
                                    uint16_t *pressure_mpa1000);

/*
 * 读取配置指定的温度 ADC 通道并换算为 0.1 摄氏度定点值。
 * ADC 无效或温度未标定时返回 -1，并把 *temperature_deg10 写为 0。
 */
int cooling_sensor_temperature_deg10(uint16_t *temperature_deg10);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_SENSOR_H */
