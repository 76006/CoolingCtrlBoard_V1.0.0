/*
 * CoolingCtrlBoard 冷媒压力加热控制核心（纯 C，可移植）——模块私有头
 *
 * 本头文件只允许压力模块内部（cooling_pressure_control.c 与
 * cooling_pressure_service.c）包含；外部模块必须包含模块唯一公共
 * 头文件 pressure/cooling_pressure.h。
 *
 * 本层只做“方案 A：GPIO 软件 PWM + 分阶段液压加热控制”的状态机与
 * 时序计算，不依赖 RT-Thread、STM32 HAL、GPIO 或 ADC 驱动，
 * 输入输出全部通过结构体传递，便于移植和单元测试。
 *
 * 全部计时使用无符号整数并做溢出饱和，不使用浮点。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_PRESSURE_COOLING_PRESSURE_CONTROL_H
#define APPLICATIONS_PRESSURE_COOLING_PRESSURE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "cooling_config.h"
#include "cooling_pressure.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 联锁原因位掩码，可组合。 */
typedef enum
{
    COOLING_PRESSURE_INTERLOCK_NONE           = 0,
    COOLING_PRESSURE_INTERLOCK_NOT_CALIBRATED = (1 << 0), /* 未标定 */
    COOLING_PRESSURE_INTERLOCK_NOT_AUTHORIZED = (1 << 1), /* UID 授权无效 */
    COOLING_PRESSURE_INTERLOCK_OTA_BUSY       = (1 << 2), /* OTA 接收中 */
    COOLING_PRESSURE_INTERLOCK_POWER_OFF      = (1 << 3), /* 主控未上电 */
    COOLING_PRESSURE_INTERLOCK_AUTO_DISABLED  = (1 << 4)  /* 自动加热控制被禁用 */
} cooling_pressure_interlock_t;

/* 单次更新输入。ADC 陈旧与否由服务层判定后归并到 adc_valid。 */
typedef struct
{
    bool calibrated;           /* 是否已标定（未标定前禁止加热） */
    bool tank_present;         /* 冷媒罐是否插入（消抖后的稳定状态） */
    bool adc_valid;            /* ADC 有效、新鲜且在合理范围内 */
    uint16_t filtered_adc;     /* 滤波后的压力 ADC 值 */
    bool authorized;           /* UID 授权有效 */
    bool ota_busy;             /* OTA 正在接收数据 */
    bool powered;              /* 主控已上电（0x1000 上电锁存） */
    bool auto_control_enabled; /* 自动加热控制启用（0x2010） */
    uint32_t dt_ms;            /* 距上次更新的时间增量 */
} cooling_pressure_input_t;

/* 单次更新输出。任何异常路径 duty_percent 都保证为 0。 */
typedef struct
{
    uint8_t duty_percent;            /* 0..100 加热占空比 */
    bool refrigerant_sufficient;     /* 冷媒充足 */
    bool refrigerant_low;            /* 冷媒不足（锁存） */
    cooling_pressure_state_t state;  /* 当前状态 */
    uint8_t interlock_mask;          /* 联锁原因位掩码 */
} cooling_pressure_output_t;

/* 状态机内部状态，由调用者持有，服务层保持跨周期调用同一实例。 */
typedef struct
{
    cooling_pressure_state_t state;
    bool sufficient;          /* 充足标志，达标确认后置位，拔罐清除 */
    bool low_latched;         /* 不足锁存，升压超时后置位，拔罐清除 */
    bool maintain_on;         /* 维持阶段的当前开关决策，实现滞回保持 */
    uint32_t boost_elapsed_ms;   /* 本轮升压+确认累计时间，饱和累加 */
    uint32_t confirm_elapsed_ms; /* 压力连续达到目标的累计时间 */
} cooling_pressure_control_t;

/* 初始化控制状态机。 */
void cooling_pressure_control_init(cooling_pressure_control_t *control);

/*
 * 无符号时间累加：delta 使结果溢出 UINT32_MAX 时饱和到 UINT32_MAX，
 * 保证任何计时比较都不会因为回绕而失效。
 */
uint32_t cooling_pressure_time_add(uint32_t elapsed_ms, uint32_t delta_ms);

/*
 * 执行一次控制状态机更新。
 * 优先级：无罐 > 未标定/未授权/OTA/未上电/自动控制禁用
 *        > ADC 故障 > 已锁存不足 > 正常控制。
 */
void cooling_pressure_control_update(cooling_pressure_control_t *control,
                                     const cooling_pressure_input_t *input,
                                     cooling_pressure_output_t *output);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_PRESSURE_COOLING_PRESSURE_CONTROL_H */
