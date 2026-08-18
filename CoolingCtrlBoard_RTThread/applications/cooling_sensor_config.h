/*
 * CoolingCtrlBoard 传感器与物理量标定配置
 *
 * 统一保存压力、温度传感器的业务通道选择、标定状态和物理量换算
 * 参数。控制服务与 CAN 状态上报读取同一份压力标定状态，避免出现
 * “控制认为已标定、通信却认为未标定”或相反的配置分裂。
 *
 * 当前没有实测标定数据，开关必须保持为 0；此时压力控制禁止加热，
 * CAN 状态帧中的压力和温度物理量编码为 0。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_SENSOR_CONFIG_H
#define APPLICATIONS_COOLING_SENSOR_CONFIG_H

#include "cooling_channels.h"

/* 系统温度使用的 ADC 业务通道。 */
#define COOLING_TEMPERATURE_ADC_INPUT            COOLING_ADC_NTC0

/* 传感器是否已经完成实测标定：0 未标定，1 已标定。 */
#define COOLING_PRESSURE_CALIBRATED               0U
#define COOLING_TEMPERATURE_CALIBRATED            0U

/* 压力线性换算：满量程对应值与零点偏移，单位 0.001 MPa。 */
#define COOLING_PRESSURE_ADC_FULL_MPA1000         0U
#define COOLING_PRESSURE_ADC_OFFSET_MPA1000       0U

/* 温度线性换算：满量程对应值与零点偏移，单位 0.1 摄氏度。 */
#define COOLING_TEMPERATURE_ADC_FULL_DEG10        0U
#define COOLING_TEMPERATURE_ADC_OFFSET_DEG10      0U

/* ---------------------------------------------------------------------
 * 编译期范围检查
 * --------------------------------------------------------------------- */

#if (COOLING_TEMPERATURE_ADC_INPUT >= COOLING_ADC_CHANNEL_COUNT)
#error "COOLING_TEMPERATURE_ADC_INPUT is not a valid ADC channel"
#endif

#if (COOLING_PRESSURE_CALIBRATED != 0U) && \
    (COOLING_PRESSURE_CALIBRATED != 1U)
#error "COOLING_PRESSURE_CALIBRATED must be 0 or 1"
#endif

#if (COOLING_TEMPERATURE_CALIBRATED != 0U) && \
    (COOLING_TEMPERATURE_CALIBRATED != 1U)
#error "COOLING_TEMPERATURE_CALIBRATED must be 0 or 1"
#endif

#if (COOLING_PRESSURE_CALIBRATED != 0U) && \
    ((COOLING_PRESSURE_ADC_FULL_MPA1000 == 0U) || \
     (COOLING_PRESSURE_ADC_FULL_MPA1000 > 1000000U))
#error "set the measured pressure conversion before enabling calibration"
#endif

#if (COOLING_TEMPERATURE_CALIBRATED != 0U) && \
    ((COOLING_TEMPERATURE_ADC_FULL_DEG10 == 0U) || \
     (COOLING_TEMPERATURE_ADC_FULL_DEG10 > 1000000U))
#error "set the measured temperature conversion before enabling calibration"
#endif

#endif /* APPLICATIONS_COOLING_SENSOR_CONFIG_H */
