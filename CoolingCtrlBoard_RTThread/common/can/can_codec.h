/*
 * CoolingCtrlBoard CAN 多字节字段编解码接口
 *
 * 声明大端序有符号/无符号 16 位和无符号 32 位读写函数，供普通业务
 * 帧与 OTA 帧共用；本层不解释字段的业务含义。
 */

#ifndef COOLING_CAN_CODEC_H
#define COOLING_CAN_CODEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t can_get_be_u16(const uint8_t *data);
int16_t can_get_be_s16(const uint8_t *data);
uint32_t can_get_be_u32(const uint8_t *data);

void can_put_be_u16(uint8_t *data, uint16_t value);
void can_put_be_s16(uint8_t *data, int16_t value);
void can_put_be_u32(uint8_t *data, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* COOLING_CAN_CODEC_H */
