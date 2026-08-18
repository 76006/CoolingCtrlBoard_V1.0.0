/*
 * CRC32 算法移植自 RT-FOTA 的 rt_fota_crc.c，保持与 RT-Thread
 * 官方 RBL 打包器一致。这里改为无全局查找表实现，降低 Bootloader RAM 占用。
 */

#include "fota_crc.h"

uint32_t fota_crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    unsigned int bit;

    if ((bytes == NULL) && (length != 0U))
    {
        return crc;
    }

    for (index = 0U; index < length; ++index)
    {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) != 0U
                    ? (crc >> 1U) ^ UINT32_C(0xEDB88320)
                    : crc >> 1U;
        }
    }

    return crc;
}

uint32_t fota_crc32_finish(uint32_t crc)
{
    return crc ^ UINT32_C(0xFFFFFFFF);
}

uint32_t fota_crc32_calculate(const void *data, size_t length)
{
    return fota_crc32_finish(fota_crc32_update(FOTA_CRC32_INITIAL_VALUE,
                                                data,
                                                length));
}

uint16_t fota_crc16_ccitt(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = UINT16_C(0xFFFF);
    size_t index;
    unsigned int bit;

    if ((bytes == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (index = 0U; index < length; ++index)
    {
        crc ^= (uint16_t)((uint16_t)bytes[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & UINT16_C(0x8000)) != 0U
                    ? (uint16_t)((crc << 1U) ^ UINT16_C(0x1021))
                    : (uint16_t)(crc << 1U);
        }
    }

    return crc;
}
