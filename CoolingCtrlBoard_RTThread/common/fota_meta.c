/*
 * CoolingCtrlBoard OTA 掉电元数据实现
 *
 * 在 ota_meta 的两个独立 Flash 页之间交替保存升级状态、RBL/App 大小
 * 和 CRC。记录先写内容、最后写提交标志，读取时选择最新有效序号，
 * 保证任意写入时刻掉电后仍至少保留一份可判定的状态记录。
 */

#include "fota_meta.h"

#include <stddef.h>
#include <string.h>

#include <fal.h>

#include "cooling_flash_layout.h"
#include "fota_crc.h"

#define FOTA_META_MAGIC          UINT32_C(0x41544F43) /* little-endian: COTA */
#define FOTA_META_SCHEMA         UINT16_C(1)
#define FOTA_META_COMMIT         UINT16_C(0xA55A)
#define FOTA_META_PAGE_COUNT     2U

typedef struct
{
    uint32_t magic;
    uint16_t schema;
    uint16_t state;
    uint32_t sequence;
    uint32_t rbl_size;
    uint32_t rbl_crc;
    uint32_t app_size;
    uint32_t app_crc;
    uint32_t reserved;
    uint32_t record_crc;
    uint16_t commit;
    uint16_t tail;
} fota_meta_record_t;

typedef char fota_meta_record_must_be_40_bytes[
        (sizeof(fota_meta_record_t) == 40U) ? 1 : -1];

static const struct fal_partition *fota_meta_partition(void)
{
    return fal_partition_find(COOLING_FAL_OTA_META_PART_NAME);
}

static int fota_meta_state_valid(uint16_t state)
{
    return state <= (uint16_t)FOTA_META_INVALID;
}

static int fota_meta_record_valid(const fota_meta_record_t *record)
{
    uint32_t crc;

    if ((record->magic != FOTA_META_MAGIC) ||
        (record->schema != FOTA_META_SCHEMA) ||
        (record->commit != FOTA_META_COMMIT) ||
        !fota_meta_state_valid(record->state))
    {
        return 0;
    }

    crc = fota_crc32_calculate(record, offsetof(fota_meta_record_t, record_crc));
    return crc == record->record_crc;
}

static int fota_meta_read_page(const struct fal_partition *part,
                               uint32_t page_index,
                               fota_meta_record_t *record)
{
    uint32_t offset = page_index * COOLING_FLASH_PAGE_SIZE;

    return fal_partition_read(part, offset, (uint8_t *)record,
                              sizeof(*record)) == (int)sizeof(*record)
            ? 0 : -1;
}

static int fota_meta_newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

static int fota_meta_select(const fota_meta_record_t records[FOTA_META_PAGE_COUNT],
                            uint32_t *selected_page)
{
    int valid0 = fota_meta_record_valid(&records[0]);
    int valid1 = fota_meta_record_valid(&records[1]);

    if (!valid0 && !valid1)
    {
        return -1;
    }

    if (valid0 && (!valid1 || fota_meta_newer(records[0].sequence,
                                               records[1].sequence)))
    {
        *selected_page = 0U;
    }
    else
    {
        *selected_page = 1U;
    }
    return 0;
}

int fota_meta_load(fota_meta_info_t *info)
{
    const struct fal_partition *part;
    fota_meta_record_t records[FOTA_META_PAGE_COUNT];
    uint32_t selected;

    if (info == NULL)
    {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->state = FOTA_META_EMPTY;
    part = fota_meta_partition();
    if (part == NULL)
    {
        return -1;
    }

    if ((fota_meta_read_page(part, 0U, &records[0]) != 0) ||
        (fota_meta_read_page(part, 1U, &records[1]) != 0))
    {
        return -1;
    }

    if (fota_meta_select(records, &selected) != 0)
    {
        /* 全新器件的两个空白页是合法的“尚无记录”状态。 */
        return 0;
    }

    info->sequence = records[selected].sequence;
    info->state = (fota_meta_state_t)records[selected].state;
    info->rbl_size = records[selected].rbl_size;
    info->rbl_crc = records[selected].rbl_crc;
    info->app_size = records[selected].app_size;
    info->app_crc = records[selected].app_crc;
    return 0;
}

int fota_meta_store(const fota_meta_info_t *info)
{
    const struct fal_partition *part;
    fota_meta_record_t records[FOTA_META_PAGE_COUNT];
    fota_meta_record_t next;
    fota_meta_record_t verify;
    uint32_t selected;
    uint32_t target_page;
    uint32_t target_offset;
    uint16_t commit = FOTA_META_COMMIT;

    if ((info == NULL) || !fota_meta_state_valid((uint16_t)info->state))
    {
        return -1;
    }

    part = fota_meta_partition();
    if (part == NULL)
    {
        return -1;
    }

    if ((fota_meta_read_page(part, 0U, &records[0]) != 0) ||
        (fota_meta_read_page(part, 1U, &records[1]) != 0))
    {
        return -1;
    }

    if (fota_meta_select(records, &selected) == 0)
    {
        target_page = selected ^ 1U;
    }
    else
    {
        target_page = 0U;
    }
    target_offset = target_page * COOLING_FLASH_PAGE_SIZE;

    memset(&next, 0xFF, sizeof(next));
    next.magic = FOTA_META_MAGIC;
    next.schema = FOTA_META_SCHEMA;
    next.state = (uint16_t)info->state;
    next.sequence = info->sequence;
    next.rbl_size = info->rbl_size;
    next.rbl_crc = info->rbl_crc;
    next.app_size = info->app_size;
    next.app_crc = info->app_crc;
    next.record_crc = fota_crc32_calculate(&next,
                              offsetof(fota_meta_record_t, record_crc));

    if (fal_partition_erase(part, target_offset,
                            COOLING_FLASH_PAGE_SIZE) !=
        (int)COOLING_FLASH_PAGE_SIZE)
    {
        return -1;
    }

    /* commit 之前的内容写完整后再提交，掉电时未提交的新页会被忽略。 */
    if (fal_partition_write(part, target_offset, (const uint8_t *)&next,
                            offsetof(fota_meta_record_t, commit)) !=
        (int)offsetof(fota_meta_record_t, commit))
    {
        return -1;
    }

    if (fal_partition_write(part,
                            target_offset + offsetof(fota_meta_record_t, commit),
                            (const uint8_t *)&commit, sizeof(commit)) !=
        (int)sizeof(commit))
    {
        return -1;
    }

    if ((fota_meta_read_page(part, target_page, &verify) != 0) ||
        !fota_meta_record_valid(&verify) ||
        (verify.sequence != next.sequence))
    {
        return -1;
    }

    return 0;
}

int fota_meta_set(fota_meta_state_t state,
                  uint32_t rbl_size,
                  uint32_t rbl_crc,
                  uint32_t app_size,
                  uint32_t app_crc)
{
    fota_meta_info_t info;

    if (fota_meta_load(&info) != 0)
    {
        return -1;
    }

    info.sequence++;
    info.state = state;
    info.rbl_size = rbl_size;
    info.rbl_crc = rbl_crc;
    info.app_size = app_size;
    info.app_crc = app_crc;
    return fota_meta_store(&info);
}
