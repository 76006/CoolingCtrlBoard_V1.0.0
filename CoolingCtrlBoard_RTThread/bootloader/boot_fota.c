/*
 * CoolingCtrlBoard Bootloader FOTA 安装器
 *
 * 启动时读取双页 OTA 元数据，只有 READY/COPYING 固件通过下载区 CRC32
 * 和 RBL 完整性校验后才擦除并覆盖 App。复制前持久化 COPYING，掉电后
 * 可从完整下载区重新开始安装；同时负责校验 App 向量表并安全跳转。
 */

#include "boot_fota.h"

#include <stdint.h>

#include <board.h>
#include <fal.h>

#include "cooling_flash_layout.h"
#include "fota_meta.h"
#include "fota_rbl.h"
#include "fota_storage.h"

#define BOOT_COPY_BUFFER_SIZE 256U

static void boot_fota_branch(uint32_t app_stack, uint32_t app_reset)
        __attribute__((naked, noreturn));

static void boot_fota_branch(uint32_t app_stack, uint32_t app_reset)
{
    (void)app_stack;
    (void)app_reset;
    __asm volatile
    (
        "msr msp, r0\n"
        "movs r2, #0\n"
        "msr control, r2\n"
        "isb\n"
        "cpsie i\n"
        "bx r1\n"
    );
}

static int boot_fota_validate_staged(const fota_meta_info_t *meta,
                                     const struct fal_partition *download,
                                     fota_rbl_header_t *header)
{
    fota_storage_reader_t reader;
    uint32_t file_crc;

    if ((meta->rbl_size < FOTA_RBL_HEADER_SIZE) ||
        (meta->rbl_size > COOLING_DOWNLOAD_SIZE) ||
        (fota_storage_crc32(download, meta->rbl_size, &file_crc) != 0) ||
        (file_crc != meta->rbl_crc))
    {
        return -1;
    }

    reader.partition = download;
    if (fota_rbl_verify(fota_storage_read, &reader, meta->rbl_size,
                        COOLING_APP_SIZE, header) != FOTA_RBL_OK)
    {
        return -1;
    }

    return ((header->raw_size == meta->app_size) &&
            (header->code_crc == meta->app_crc)) ? 0 : -1;
}

int boot_fota_install_pending(void)
{
    const struct fal_partition *download;
    const struct fal_partition *app;
    fota_meta_info_t meta;
    fota_rbl_header_t header;
    uint8_t buffer[BOOT_COPY_BUFFER_SIZE];
    uint32_t position;
    uint32_t app_crc;

    if (fal_init() < 0)
    {
        return -1;
    }

    if (fota_meta_load(&meta) != 0)
    {
        return -1;
    }

    if ((meta.state != FOTA_META_READY) &&
        (meta.state != FOTA_META_COPYING))
    {
        return 0;
    }

    download = fal_partition_find(COOLING_FAL_DOWNLOAD_PART_NAME);
    app = fal_partition_find(COOLING_FAL_APP_PART_NAME);
    if ((download == NULL) || (app == NULL))
    {
        return -1;
    }

    /* READY 阶段的任何校验失败都不会擦除原 App。 */
    if (boot_fota_validate_staged(&meta, download, &header) != 0)
    {
        (void)fota_meta_set(FOTA_META_INVALID, meta.rbl_size, meta.rbl_crc,
                            meta.app_size, meta.app_crc);
        return -1;
    }

    /* 先持久化 COPYING，再擦 App。若此后掉电，下次启动会从头重新复制。 */
    if ((meta.state != FOTA_META_COPYING) &&
        (fota_meta_set(FOTA_META_COPYING, meta.rbl_size, meta.rbl_crc,
                       header.raw_size, header.code_crc) != 0))
    {
        return -1;
    }

    if (fal_partition_erase_all(app) != (int)app->len)
    {
        return -1;
    }

    position = 0U;
    while (position < header.raw_size)
    {
        uint32_t remaining = header.raw_size - position;
        uint32_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        if ((fal_partition_read(download, FOTA_RBL_HEADER_SIZE + position,
                                buffer, chunk) != (int)chunk) ||
            (fal_partition_write(app, position, buffer, chunk) != (int)chunk))
        {
            /* 保留 COPYING，重启后仍能使用完整下载区恢复。 */
            return -1;
        }
        position += chunk;
    }

    if ((fota_storage_crc32(app, header.raw_size, &app_crc) != 0) ||
        (app_crc != header.code_crc))
    {
        return -1;
    }

    if (fota_meta_set(FOTA_META_INSTALLED, meta.rbl_size, meta.rbl_crc,
                      header.raw_size, header.code_crc) != 0)
    {
        return -1;
    }
    return 1;
}

int boot_fota_app_is_valid(void)
{
    const uint32_t *vectors = (const uint32_t *)COOLING_APP_START_ADDRESS;
    uint32_t stack = vectors[0];
    uint32_t reset = vectors[1];
    uint32_t sram_end = UINT32_C(0x20000000) +
                        (UINT32_C(40) * UINT32_C(1024));

    return (stack >= UINT32_C(0x20000000)) &&
           (stack <= sram_end) && ((stack & UINT32_C(3)) == 0U) &&
           ((reset & UINT32_C(1)) != 0U) &&
           ((reset & ~UINT32_C(1)) >= COOLING_APP_START_ADDRESS) &&
           ((reset & ~UINT32_C(1)) <
            (COOLING_APP_START_ADDRESS + COOLING_APP_SIZE));
}

void boot_fota_jump_to_app(void)
{
    const uint32_t *vectors = (const uint32_t *)COOLING_APP_START_ADDRESS;
    uint32_t app_stack = vectors[0];
    uint32_t app_reset = vectors[1];
    uint32_t index;

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (index = 0U; index < 8U; ++index)
    {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }

    (void)HAL_RCC_DeInit();
    (void)HAL_DeInit();
    SCB->VTOR = COOLING_APP_START_ADDRESS;
    __DSB();
    __ISB();

    /* 裸函数在改写 MSP 后不再访问 Bootloader 的旧栈。 */
    boot_fota_branch(app_stack, app_reset);
}
