/*
 * CoolingCtrlBoard FOTA 存储适配接口
 *
 * 使用轻量 reader 绑定一个 FAL 分区，并提供通用读取回调和分区
 * CRC32 接口，用于隔离 RBL 解析器与具体 Flash 实现。
 */

#ifndef FOTA_STORAGE_H
#define FOTA_STORAGE_H

#include <stddef.h>
#include <stdint.h>

struct fal_partition;

typedef struct
{
    const struct fal_partition *partition;
} fota_storage_reader_t;

int fota_storage_read(void *context, uint32_t offset,
                      uint8_t *buffer, size_t size);
int fota_storage_crc32(const struct fal_partition *partition,
                       uint32_t size, uint32_t *crc_out);

#endif /* FOTA_STORAGE_H */
