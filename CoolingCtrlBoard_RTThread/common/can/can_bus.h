/*
 * CoolingCtrlBoard 统一 CAN 总线管理接口
 *
 * 定义总线启动参数、软件过滤器、接收订阅回调、统一发送接口和统计
 * 快照。业务模块只订阅自己关心的报文，不能各自重复打开 CAN 设备。
 */

#ifndef COOLING_CAN_BUS_H
#define COOLING_CAN_BUS_H

#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_BUS_FORMAT_STANDARD_MASK    (1U << CAN_FRAME_FORMAT_STANDARD)
#define CAN_BUS_FORMAT_EXTENDED_MASK    (1U << CAN_FRAME_FORMAT_EXTENDED)
#define CAN_BUS_FORMAT_ANY_MASK         (CAN_BUS_FORMAT_STANDARD_MASK | \
                                         CAN_BUS_FORMAT_EXTENDED_MASK)

#define CAN_BUS_TYPE_DATA_MASK          (1U << CAN_FRAME_TYPE_DATA)
#define CAN_BUS_TYPE_REMOTE_MASK        (1U << CAN_FRAME_TYPE_REMOTE)
#define CAN_BUS_TYPE_ANY_MASK           (CAN_BUS_TYPE_DATA_MASK | \
                                         CAN_BUS_TYPE_REMOTE_MASK)

typedef struct
{
    const char *device_name;
    uint32_t baud_rate;
    uint16_t rx_thread_stack_size;
    uint8_t rx_thread_priority;
    uint8_t rx_thread_timeslice;
} can_bus_config_t;

typedef struct
{
    uint32_t id;
    uint32_t id_mask;
    uint8_t format_mask;
    uint8_t type_mask;
} can_bus_filter_t;

typedef enum
{
    CAN_BUS_RX_PASS = 0,
    CAN_BUS_RX_CONSUMED = 1
} can_bus_rx_result_t;

typedef can_bus_rx_result_t (*can_bus_rx_handler_t)(const can_frame_t *frame,
                                                    void *context);

typedef struct
{
    uint32_t rx_frames;
    uint32_t rx_unhandled;
    uint32_t rx_errors;
    uint32_t tx_frames;
    uint32_t tx_errors;
} can_bus_stats_t;

int can_bus_start(const can_bus_config_t *config);
int can_bus_is_started(void);

/* 订阅项在启动阶段注册，运行期间不删除，避免出现悬空回调。 */
int can_bus_subscribe(const can_bus_filter_t *filter,
                      can_bus_rx_handler_t handler,
                      void *context);

int can_bus_send(const can_frame_t *frame);
void can_bus_get_stats(can_bus_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* COOLING_CAN_BUS_H */
