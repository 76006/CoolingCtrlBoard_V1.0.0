/*
 * CoolingCtrlBoard CAN 多字节字段编解码实现
 *
 * 提供 16/32 位整数与 CAN 数据数组之间的大端序转换。协议业务统一
 * 使用这些函数，避免在各模块重复位移并产生端序不一致。
 */

#include "can_codec.h"

uint16_t can_get_be_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

int16_t can_get_be_s16(const uint8_t *data)
{
    return (int16_t)can_get_be_u16(data);
}

uint32_t can_get_be_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

void can_put_be_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

void can_put_be_s16(uint8_t *data, int16_t value)
{
    can_put_be_u16(data, (uint16_t)value);
}

void can_put_be_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}
