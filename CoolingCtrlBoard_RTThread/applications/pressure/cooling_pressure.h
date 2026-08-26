/* CoolingCtrlBoard pressure control public interface. */

#ifndef APPLICATIONS_PRESSURE_COOLING_PRESSURE_H
#define APPLICATIONS_PRESSURE_COOLING_PRESSURE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Real pressure-control operating states. */
typedef enum
{
    COOLING_PRESSURE_STATE_STOP = 0,
    COOLING_PRESSURE_STATE_HEATING,
    COOLING_PRESSURE_STATE_HOLD,
    COOLING_PRESSURE_STATE_FAULT,
    COOLING_PRESSURE_STATE_COUNT
} cooling_pressure_state_t;

/* Reasons that currently prevent pressure control from heating. */
#define COOLING_PRESSURE_BLOCK_NONE             0U
#define COOLING_PRESSURE_BLOCK_NOT_CALIBRATED   (1UL << 0)
#define COOLING_PRESSURE_BLOCK_UNAUTHORIZED     (1UL << 1)
#define COOLING_PRESSURE_BLOCK_OTA_BUSY         (1UL << 2)
#define COOLING_PRESSURE_BLOCK_POWER_OFF        (1UL << 3)
#define COOLING_PRESSURE_BLOCK_AUTO_DISABLED    (1UL << 4)
#define COOLING_PRESSURE_BLOCK_NO_TANK          (1UL << 5)
#define COOLING_PRESSURE_BLOCK_ADC_INVALID      (1UL << 6)
#define COOLING_PRESSURE_BLOCK_TEMP_INVALID     (1UL << 7)

/* 供 CAN 等上层任务读取的压力控制快照。 */
typedef struct
{
    rt_uint32_t sequence;               /* 快照序号，每周期递增 */
    rt_tick_t tick;                     /* 最近更新时间 */
    rt_uint16_t raw;                    /* HYSN2 原始 ADC 值 */
    rt_uint16_t filtered;               /* 滤波后 ADC 值 */
    rt_uint16_t millivolts;             /* 换算毫伏值 */
    rt_bool_t adc_valid;                /* 有效位 + 新鲜度 + 合理范围 */
    rt_int16_t temperature_deg10;       /* NTC1 当前温度，0.1 摄氏度 */
    rt_int16_t target_temperature_deg10;/* 当前目标温度，0.1 摄氏度 */
    rt_bool_t temperature_valid;        /* NTC1 有效位 + 新鲜度 + 温阻表 */
    rt_bool_t warmup_complete;          /* 本轮已经到达过升温目标 */
    rt_bool_t tank_present;             /* 统一安全快照中的罐体存在状态 */
    cooling_pressure_state_t state;     /* 当前真实运行状态 */
    rt_uint8_t duty_percent;            /* 当前命令占空比 0..100 */
    rt_bool_t heater_on;                /* 实际加热开关（加热模块快照） */
    rt_bool_t refrigerant_sufficient;   /* 冷媒充足 */
    rt_bool_t refrigerant_low;          /* 冷媒不足（锁存） */
    rt_uint32_t block_reason;           /* 当前禁止加热的原因位掩码 */
} cooling_pressure_snapshot_t;

/*
 * 启动压力控制服务。必须在其他业务模块完成初始化尝试后调用；
 * ADC 或罐体任务失败时，无效快照会使控制核心保持关闭。
 */
rt_err_t cooling_pressure_start(void);

/* 复制一份线程安全的压力控制快照；不能在中断中调用。 */
void cooling_pressure_get_snapshot(cooling_pressure_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_PRESSURE_COOLING_PRESSURE_H */
