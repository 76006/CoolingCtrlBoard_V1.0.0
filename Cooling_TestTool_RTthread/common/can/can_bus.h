/* CoolingCtrlBoard 固定 CAN 总线接口。 */

#ifndef COOLING_CAN_BUS_H
#define COOLING_CAN_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FRAME_DATA_SIZE 8U

typedef enum
{
    CAN_FRAME_FORMAT_STANDARD = 0,
    CAN_FRAME_FORMAT_EXTENDED = 1
} can_frame_format_t;

typedef enum
{
    CAN_FRAME_TYPE_DATA = 0,
    CAN_FRAME_TYPE_REMOTE = 1
} can_frame_type_t;

typedef struct
{
    uint32_t id;
    uint8_t format;
    uint8_t type;
    uint8_t dlc;
    uint8_t data[CAN_FRAME_DATA_SIZE];
} can_frame_t;

typedef void (*can_bus_rx_handler_t)(const can_frame_t *frame);

/* 固定启动 can1：500 kbit/s，接收线程栈 2048、优先级 18。 */
int can_bus_start(can_bus_rx_handler_t handler);
int can_bus_send(const can_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* COOLING_CAN_BUS_H */
