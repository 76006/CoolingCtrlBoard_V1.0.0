/*
 * RT-Thread RBL 兼容层。
 *
 * 第一版只接受“不压缩、不加密”的标准 RBL。正文因而就是 App BIN，
 * 可以使用同一个 code_crc 同时验证下载区正文和最终 App 区。
 */

#include "fota_rbl.h"

#include <string.h>

#include "cooling_flash_layout.h"
#include "fota_crc.h"

typedef char fota_rbl_header_must_be_96_bytes[
        (sizeof(fota_rbl_header_t) == FOTA_RBL_HEADER_SIZE) ? 1 : -1];

static int fota_rbl_part_name_is_app(const char name[FOTA_RBL_PART_NAME_SIZE])
{
    static const char expected[] = COOLING_FAL_APP_PART_NAME;
    const size_t expected_length = sizeof(expected) - 1U;

    return (memcmp(name, expected, expected_length) == 0) &&
           (name[expected_length] == '\0');
}

static fota_rbl_result_t fota_rbl_read_and_validate_header(
        fota_rbl_read_fn read_fn,
        void *context,
        uint32_t source_capacity,
        uint32_t app_capacity,
        fota_rbl_header_t *header)
{
    uint32_t calculated_crc;

    if (read_fn(context, 0U, (uint8_t *)header, sizeof(*header)) !=
        (int)sizeof(*header))
    {
        return FOTA_RBL_ERROR_READ;
    }

    calculated_crc = fota_crc32_calculate(header, sizeof(*header) - sizeof(uint32_t));
    if (calculated_crc != header->header_crc)
    {
        return FOTA_RBL_ERROR_HEADER_CRC;
    }

    if ((header->type[0] != 'R') || (header->type[1] != 'B') ||
        (header->type[2] != 'L') || (header->type[3] != '\0'))
    {
        return FOTA_RBL_ERROR_MAGIC;
    }

    if (header->algorithm != FOTA_RBL_ALGORITHM_NONE)
    {
        return FOTA_RBL_ERROR_ALGORITHM;
    }

    if (!fota_rbl_part_name_is_app(header->app_part_name))
    {
        return FOTA_RBL_ERROR_PARTITION;
    }

    if ((header->raw_size == 0U) ||
        (header->raw_size > app_capacity) ||
        (header->package_size != header->raw_size) ||
        (header->package_size != (source_capacity - FOTA_RBL_HEADER_SIZE)))
    {
        return FOTA_RBL_ERROR_SIZE;
    }

    return FOTA_RBL_OK;
}

static fota_rbl_result_t fota_rbl_verify_body(
        fota_rbl_read_fn read_fn,
        void *context,
        const fota_rbl_header_t *header)
{
    uint8_t buffer[256];
    uint32_t crc = FOTA_CRC32_INITIAL_VALUE;
    uint32_t position = 0U;

    while (position < header->package_size)
    {
        const uint32_t remaining = header->package_size - position;
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        if (read_fn(context,
                    FOTA_RBL_HEADER_SIZE + position,
                    buffer,
                    chunk) != (int)chunk)
        {
            return FOTA_RBL_ERROR_READ;
        }

        crc = fota_crc32_update(crc, buffer, chunk);
        position += (uint32_t)chunk;
    }

    return fota_crc32_finish(crc) == header->code_crc
            ? FOTA_RBL_OK
            : FOTA_RBL_ERROR_BODY_CRC;
}

fota_rbl_result_t fota_rbl_verify(fota_rbl_read_fn read_fn,
                                   void *context,
                                   uint32_t source_capacity,
                                   uint32_t app_capacity,
                                   fota_rbl_header_t *header)
{
    fota_rbl_result_t result;

    result = fota_rbl_read_and_validate_header(read_fn,
                                                context,
                                                source_capacity,
                                                app_capacity,
                                                header);
    if (result != FOTA_RBL_OK)
    {
        return result;
    }

    return fota_rbl_verify_body(read_fn, context, header);
}
