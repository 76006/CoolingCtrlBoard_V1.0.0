/*
 * OTA 掉电状态记录。
 *
 * ota_meta 分区由两个独立的 2 KiB Flash 页组成。每次更新只擦除并写入
 * 非当前页，最后再写入 commit 标记，因此任意写入时刻掉电都至少保留
 * 一份有效记录。
 */

#ifndef FOTA_META_H
#define FOTA_META_H

#include <stdint.h>

typedef enum
{
    FOTA_META_EMPTY = 0,
    FOTA_META_DOWNLOADING = 1,
    FOTA_META_READY = 2,
    FOTA_META_COPYING = 3,
    FOTA_META_INSTALLED = 4,
    FOTA_META_INVALID = 5
} fota_meta_state_t;

typedef struct
{
    uint32_t sequence;
    fota_meta_state_t state;
    uint32_t rbl_size;
    uint32_t rbl_crc;
    uint32_t app_size;
    uint32_t app_crc;
} fota_meta_info_t;

/* 未写入过状态页时返回默认 EMPTY 记录，返回值仍为 0。 */
int fota_meta_load(fota_meta_info_t *info);

/* 采用双页交替和最后提交标记，原记录在新记录完全写好前不会被擦除。 */
int fota_meta_set(fota_meta_state_t state,
                  uint32_t rbl_size,
                  uint32_t rbl_crc,
                  uint32_t app_size,
                  uint32_t app_crc);

#endif /* FOTA_META_H */
