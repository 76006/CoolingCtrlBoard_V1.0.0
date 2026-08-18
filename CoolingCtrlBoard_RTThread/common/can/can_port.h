/*
 * CoolingCtrlBoard CAN 底层端口接口
 *
 * 定义统一总线层所需的打开、关闭、读写及接收通知函数，把 RTOS/驱动
 * 相关类型隔离在实现文件之外。当前实现为 can_port_rtthread.c；更换
 * RTOS 或 CAN 驱动时只需替换端口实现，上层协议保持不变。
 */

#ifndef COOLING_CAN_PORT_H
#define COOLING_CAN_PORT_H

#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*can_port_rx_notify_t)(void *context);

typedef struct
{
    const char *device_name;
    uint32_t baud_rate;
    can_port_rx_notify_t rx_notify;
    void *rx_context;
} can_port_config_t;

int can_port_open(const can_port_config_t *config);
void can_port_close(void);

/* 返回 1 表示读到一帧，0 表示暂时无帧，负数表示驱动错误。 */
int can_port_read(can_frame_t *frame);
int can_port_write(const can_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* COOLING_CAN_PORT_H */
