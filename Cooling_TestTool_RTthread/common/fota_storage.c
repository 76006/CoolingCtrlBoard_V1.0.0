/*
 * CoolingCtrlBoard FOTA 存储适配实现
 *
 * 负责打开 App/download FAL 分区，向 RBL 解析器提供统一的带边界读取
 * 回调，并按块计算指定分区内容的 CRC32。协议层无需了解具体 Flash
 * 地址和底层驱动。
 */

#include "fota_storage.h"

#include <fal.h>

#include "cooling_flash_layout.h"
#include "fota_crc.h"

int fota_storage_read(void *context, uint32_t offset,
                      uint8_t *buffer, size_t size)
{
    fota_storage_reader_t *reader = (fota_storage_reader_t *)context;

    return fal_partition_read(reader->partition, offset, buffer, size);
}

int fota_storage_crc32(const struct fal_partition *partition,
                       uint32_t size, uint32_t *crc_out)
{
    uint8_t buffer[256];
    uint32_t position = 0U;
    uint32_t crc = FOTA_CRC32_INITIAL_VALUE;

    while (position < size)
    {
        uint32_t remaining = size - position;
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        if (fal_partition_read(partition, position, buffer, chunk) != (int)chunk)
        {
            return -1;
        }
        crc = fota_crc32_update(crc, buffer, chunk);
        position += (uint32_t)chunk;
    }

    *crc_out = fota_crc32_finish(crc);
    return 0;
}
