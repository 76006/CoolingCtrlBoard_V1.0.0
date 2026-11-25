/*
 * CoolingCtrlBoard FOTA 校验算法接口
 *
 * 提供与 RT-Thread RBL 打包格式兼容的 CRC32，以及 CAN 接收整包校验
 * 使用的 CRC16-CCITT-FALSE。CRC32 同时提供增量、结束和一次性接口。
 */

#ifndef FOTA_CRC_H
#define FOTA_CRC_H

#include <stddef.h>
#include <stdint.h>

#define FOTA_CRC32_INITIAL_VALUE UINT32_C(0xFFFFFFFF)

uint32_t fota_crc32_update(uint32_t crc, const void *data, size_t length);
uint32_t fota_crc32_finish(uint32_t crc);
uint32_t fota_crc32_calculate(const void *data, size_t length);
uint16_t fota_crc16_ccitt(const void *data, size_t length);

#endif /* FOTA_CRC_H */
