/*
 * CoolingCtrlBoard RT-Thread RBL 解析与校验接口
 *
 * 定义 96 字节 RBL 头结构、错误码和抽象读取回调，提供头部 CRC、魔数、
 * 算法、目标分区、长度及正文 CRC32 校验。读取回调使解析器不依赖 FAL。
 */

#ifndef FOTA_RBL_H
#define FOTA_RBL_H

#include <stddef.h>
#include <stdint.h>

#define FOTA_RBL_HEADER_SIZE        96U
#define FOTA_RBL_PART_NAME_SIZE     16U
#define FOTA_RBL_VERSION_SIZE       24U
#define FOTA_RBL_ALGORITHM_NONE     UINT16_C(0x0000)

typedef struct
{
    char type[4];
    uint16_t algorithm;
    uint8_t build_time[6];
    char app_part_name[FOTA_RBL_PART_NAME_SIZE];
    char download_version[FOTA_RBL_VERSION_SIZE];
    char current_version[FOTA_RBL_VERSION_SIZE];
    uint32_t code_crc;
    uint32_t hash_value;
    uint32_t raw_size;
    uint32_t package_size;
    uint32_t header_crc;
} fota_rbl_header_t;

typedef enum
{
    FOTA_RBL_OK = 0,
    FOTA_RBL_ERROR_READ = -1,
    FOTA_RBL_ERROR_HEADER_CRC = -2,
    FOTA_RBL_ERROR_MAGIC = -3,
    FOTA_RBL_ERROR_ALGORITHM = -4,
    FOTA_RBL_ERROR_PARTITION = -5,
    FOTA_RBL_ERROR_SIZE = -6,
    FOTA_RBL_ERROR_BODY_CRC = -7
} fota_rbl_result_t;

typedef int (*fota_rbl_read_fn)(void *context,
                                uint32_t offset,
                                uint8_t *buffer,
                                size_t size);

fota_rbl_result_t fota_rbl_verify(fota_rbl_read_fn read_fn,
                                   void *context,
                                   uint32_t source_capacity,
                                   uint32_t app_capacity,
                                   fota_rbl_header_t *header);

#endif /* FOTA_RBL_H */
