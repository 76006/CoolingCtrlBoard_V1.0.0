/*
 * CoolingCtrlBoard 冷媒压力加热控制服务公共接口（模块唯一公共头文件）
 *
 * 其他模块（CAN、main 等）只能包含本文件获取压力控制快照；
 * 压力控制的纯 C 核心（cooling_pressure_control.h）属于模块私有
 * 实现，外部模块禁止包含。
 *
 * 本服务以独立 RT-Thread 任务周期运行：读取 ADC 快照与统一安全
 * 快照、完成有效性与新鲜度校验、调用纯 C 控制核心，并用固定窗口
 * 软件 PWM 通过加热模块接口驱动加热输出。所有加热命令都经过
 * 统一安全模块联锁，本服务不提供绕过联锁的手动开加热命令。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_PRESSURE_COOLING_PRESSURE_H
#define APPLICATIONS_PRESSURE_COOLING_PRESSURE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 控制状态机状态。 */
typedef enum
{
    COOLING_PRESSURE_STATE_NOT_CALIBRATED = 0, /* 未标定：禁止加热 */
    COOLING_PRESSURE_STATE_NO_TANK,            /* 无罐：立即关断并重置本轮 */
    COOLING_PRESSURE_STATE_BOOST,              /* 升压加热：满功率升压 */
    COOLING_PRESSURE_STATE_CONFIRM,            /* 压力确认：已达目标，连续保持确认中 */
    COOLING_PRESSURE_STATE_MAINTAIN,           /* 低功率维持：按上下滞回维持 */
    COOLING_PRESSURE_STATE_LOW,                /* 冷媒不足：锁存到拔罐 */
    COOLING_PRESSURE_STATE_SENSOR_FAULT,       /* 传感器故障：无效或陈旧 ADC */
    COOLING_PRESSURE_STATE_INTERLOCK,          /* 安全联锁：未标定/未授权/OTA 忙 */
    COOLING_PRESSURE_STATE_COUNT
} cooling_pressure_state_t;

/* 供 CAN 等上层任务读取的压力控制快照。 */
typedef struct
{
    rt_uint32_t sequence;               /* 快照序号，每周期递增 */
    rt_tick_t tick;                     /* 最近更新时间 */
    rt_uint16_t raw;                    /* HYSN2 原始 ADC 值 */
    rt_uint16_t filtered;               /* 滤波后 ADC 值 */
    rt_uint16_t millivolts;             /* 换算毫伏值 */
    rt_bool_t adc_valid;                /* 有效位 + 新鲜度 + 合理范围 */
    rt_bool_t tank_present;             /* 统一安全快照中的罐体存在状态 */
    cooling_pressure_state_t state;     /* 控制状态机状态 */
    rt_uint8_t duty_percent;            /* 当前命令占空比 0..100 */
    rt_bool_t heater_on;                /* 实际加热开关（加热模块快照） */
    rt_bool_t refrigerant_sufficient;   /* 冷媒充足 */
    rt_bool_t refrigerant_low;          /* 冷媒不足（锁存） */
    rt_uint8_t interlock_mask;          /* 联锁原因位掩码 */
} cooling_pressure_snapshot_t;

/*
 * 启动压力控制服务。必须在统一安全模块、加热模块、罐体模块与
 * ADC 采样均成功后调用；重复调用幂等；任何失败路径都保证
 * 加热输出保持关闭。
 */
rt_err_t cooling_pressure_start(void);

/* 复制一份线程安全的压力控制快照；不能在中断中调用。 */
rt_err_t cooling_pressure_get_snapshot(cooling_pressure_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_PRESSURE_COOLING_PRESSURE_H */
