/*
 * CoolingCtrlBoard CAN 扩展帧 ID 编解码实现
 *
 * 该文件属于纯协议层，负责把“源节点、目标节点、帧类别、功能码”
 * 组装为 29 位扩展 ID，也负责反向解析和本机/广播目标判断。
 * 不访问 CAN 硬件，不处理具体业务，便于在其他 MCU 上直接复用。
 */

#include "cooling_can_protocol.h"

#include <stddef.h>

#include "can/can_frame.h"

int cooling_can_make_id(const cooling_can_id_fields_t *fields, uint32_t *id)
{
    if ((fields == NULL) || (id == NULL) ||
        (fields->source > COOLING_CAN_NODE_MAX) ||
        (fields->target > COOLING_CAN_NODE_MAX) ||
        (fields->frame_class > COOLING_CAN_CLASS_MAX))
    {
        return -1;
    }

    *id = ((uint32_t)fields->source << 24U) |
          ((uint32_t)fields->target << 19U) |
          ((uint32_t)fields->frame_class << 16U) |
          fields->function;
    return 0;
}

int cooling_can_parse_id(uint32_t id, cooling_can_id_fields_t *fields)
{
    if ((fields == NULL) || (id > CAN_FRAME_EXT_ID_MAX))
    {
        return -1;
    }

    fields->source = (uint8_t)((id >> 24U) & COOLING_CAN_NODE_MAX);
    fields->target = (uint8_t)((id >> 19U) & COOLING_CAN_NODE_MAX);
    fields->frame_class = (uint8_t)((id >> 16U) & COOLING_CAN_CLASS_MAX);
    fields->function = (uint16_t)id;
    return 0;
}

int cooling_can_is_for_node(const cooling_can_id_fields_t *fields,
                            uint8_t local_node)
{
    if ((fields == NULL) || (local_node == COOLING_CAN_NODE_BROADCAST) ||
        (local_node > COOLING_CAN_NODE_MAX))
    {
        return 0;
    }

    return (fields->target == local_node) ||
           (fields->target == COOLING_CAN_NODE_BROADCAST);
}
