/*
 * 与操作系统和芯片无关的经典 CAN 帧定义。
 *
 * 协议层只依赖本文件，不能直接依赖 RT-Thread 的 rt_can_msg，便于后续移植。
 */

#ifndef COOLING_CAN_FRAME_H
#define COOLING_CAN_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FRAME_DATA_SIZE       8U
#define CAN_FRAME_STD_ID_MAX      UINT32_C(0x7FF)
#define CAN_FRAME_EXT_ID_MAX      UINT32_C(0x1FFFFFFF)

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

#ifdef __cplusplus
}
#endif

#endif /* COOLING_CAN_FRAME_H */
