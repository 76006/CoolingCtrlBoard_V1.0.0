/*
 * CoolingCtrlBoard 29 位扩展帧 ID 的纯协议定义。
 *
 * ID 布局：bits28..24 源节点，bits23..19 目标节点，
 * bits18..16 帧类别，bits15..0 功能码/OTA 帧序号。
 * 多字节数据一律大端，编解码统一走 common/can 的 can_codec。
 */

#ifndef APPLICATIONS_COOLING_CAN_PROTOCOL_H
#define APPLICATIONS_COOLING_CAN_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COOLING_CAN_NODE_BROADCAST        0x00U
#define COOLING_CAN_NODE_MAX              0x1FU
#define COOLING_CAN_CLASS_MAX             0x07U

typedef enum
{
    COOLING_CAN_CLASS_NORMAL = 0,  /* 普通业务帧 */
    COOLING_CAN_CLASS_STREAM = 1,  /* 类别 1 专用于 OTA 数据帧 */
    COOLING_CAN_CLASS_EVENT = 2    /* 预留事件类，当前未使用 */
} cooling_can_frame_class_t;

/* 类别 1 的语义别名：OTA 数据帧，ID 低 16 位为 8 字节帧序号。 */
#define COOLING_CAN_CLASS_OTA_DATA        COOLING_CAN_CLASS_STREAM

/* 功能码（普通类帧的 ID 低 16 位）。 */
#define COOLING_CAN_FUNC_POWER_BROADCAST  0x1000U  /* 主控->广播：上/下电 */
#define COOLING_CAN_FUNC_HANDSHAKE_REQ    0x2000U  /* 主控->制冷：握手请求 */
#define COOLING_CAN_FUNC_AUTO_HEAT        0x2010U  /* 主控->制冷：自动加热控制 */
#define COOLING_CAN_FUNC_HANDSHAKE_REPLY  0x2100U  /* 制冷->主控：握手应答 */
#define COOLING_CAN_FUNC_STATUS           0x2110U  /* 制冷->主控：周期状态 */
#define COOLING_CAN_FUNC_ERROR            0x2200U  /* 制冷->主控：错误帧（当前保留，绝不主动发送） */
#define COOLING_CAN_FUNC_OTA_REQUEST      0x2210U  /* 主控->制冷：OTA 请求；制冷->主控：准备应答 */
#define COOLING_CAN_FUNC_OTA_COMPLETE     0x2220U  /* 制冷->主控：OTA 完成 */

/* 各帧固定 DLC。 */
#define COOLING_CAN_DLC_POWER_BROADCAST   4U
#define COOLING_CAN_DLC_HANDSHAKE         8U
#define COOLING_CAN_DLC_AUTO_HEAT         2U
#define COOLING_CAN_DLC_STATUS            8U
#define COOLING_CAN_DLC_OTA_REQUEST       8U
#define COOLING_CAN_DLC_OTA_ACK           2U
#define COOLING_CAN_DLC_OTA_COMPLETE      4U

/* 上/下电广播与自动加热控制的取值。 */
#define COOLING_CAN_POWER_ON              1U
#define COOLING_CAN_POWER_OFF             0U
#define COOLING_CAN_AUTO_HEAT_ON          1U
#define COOLING_CAN_AUTO_HEAT_OFF         0U

/* OTA 逐帧 ACK 与准备/完成应答的取值。 */
#define COOLING_CAN_OTA_RESULT_OK         1U
#define COOLING_CAN_OTA_RESULT_FAIL       0U

/* OTA 完成失败原因（0x2220 data1）。 */
typedef enum
{
    COOLING_CAN_OTA_FAIL_CRC16 = 0,    /* 整包 CRC16 校验失败 */
    COOLING_CAN_OTA_FAIL_TIMEOUT = 1,  /* 帧间超时 */
    COOLING_CAN_OTA_FAIL_OTHER = 2     /* 其他（RBL/Flash/元数据失败） */
} cooling_can_ota_fail_reason_t;

/* 冷媒剩余量上报编码（状态帧 data2，初版离散映射，不做 1..100 连续换算）。 */
#define COOLING_CAN_REFRIGERANT_NONE        0U    /* 无罐/低压超时/传感器无效或未标定 */
#define COOLING_CAN_REFRIGERANT_SUFFICIENT  100U  /* 冷媒充足 */
#define COOLING_CAN_REFRIGERANT_CHARGING    255U  /* 刚插入/升压/确认中 */

/* OTA 数据帧：每帧最多 8 字节，ID 低 16 位为帧序号（不是字节地址），
 * offset = seq * 8，最后一帧 DLC 为实际剩余长度。 */
#define COOLING_CAN_OTA_FRAME_DATA_SIZE    8U
#define COOLING_CAN_OTA_SEQ_MAX            0xFFFFU

typedef struct
{
    uint8_t source;
    uint8_t target;
    uint8_t frame_class;
    uint16_t function;
} cooling_can_id_fields_t;

int cooling_can_make_id(const cooling_can_id_fields_t *fields, uint32_t *id);
int cooling_can_parse_id(uint32_t id, cooling_can_id_fields_t *fields);
int cooling_can_is_for_node(const cooling_can_id_fields_t *fields,
                            uint8_t local_node);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_CAN_PROTOCOL_H */
