/*
 * STM32F302CCT6 内部 Flash 的 FAL 端口。
 *
 * 写入和擦除全部进行范围、溢出、页对齐及回读检查。App 镜像只能写下载区
 * 及其后的数据区；只有 Bootloader 构建才允许写 App 区。两者都不能写自身。
 */

#include <fal.h>
#include <string.h>

#include "stm32f3xx_hal.h"
#include "cooling_flash_layout.h"

static int cooling_flash_range_valid(uint32_t address, size_t size)
{
    if (size == 0U)
    {
        return 1;
    }

    return (address >= COOLING_FLASH_BASE_ADDRESS) &&
           (address < COOLING_FLASH_END_ADDRESS) &&
           (size <= (size_t)(COOLING_FLASH_END_ADDRESS - address));
}

static int cooling_flash_write_allowed(uint32_t address, size_t size)
{
#ifdef COOLING_BOOTLOADER_BUILD
    const uint32_t writable_start = COOLING_APP_START_ADDRESS;
#else
    const uint32_t writable_start = COOLING_DOWNLOAD_START_ADDRESS;
#endif

    return cooling_flash_range_valid(address, size) &&
           (address >= writable_start);
}

static int cooling_flash_init(void)
{
    /* 芯片容量寄存器单位是 KiB；容量不符时禁止后续越界访问。 */
    return (*((const uint16_t *)FLASH_SIZE_DATA_REGISTER) >= 256U) ? 0 : -1;
}

static int cooling_flash_read(long offset, uint8_t *buffer, size_t size)
{
    uint32_t address;

    if ((offset < 0) || ((buffer == NULL) && (size != 0U)))
    {
        return -1;
    }

    address = COOLING_FLASH_BASE_ADDRESS + (uint32_t)offset;
    if (!cooling_flash_range_valid(address, size))
    {
        return -1;
    }

    if (size != 0U)
    {
        memcpy(buffer, (const void *)address, size);
    }
    return (int)size;
}

static int cooling_flash_write(long offset, const uint8_t *buffer, size_t size)
{
    uint32_t address;
    uint32_t first_halfword;
    uint32_t end_address;
    uint32_t halfword_address;
    HAL_StatusTypeDef status = HAL_OK;

    if ((offset < 0) || ((buffer == NULL) && (size != 0U)))
    {
        return -1;
    }

    address = COOLING_FLASH_BASE_ADDRESS + (uint32_t)offset;
    if (!cooling_flash_write_allowed(address, size))
    {
        return -1;
    }
    if (size == 0U)
    {
        return 0;
    }

    first_halfword = address & ~UINT32_C(1);
    end_address = address + (uint32_t)size;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    for (halfword_address = first_halfword;
         halfword_address < end_address;
         halfword_address += 2U)
    {
        const uint16_t old_value = *((const uint16_t *)halfword_address);
        uint16_t new_value = old_value;
        unsigned int byte_index;

        for (byte_index = 0U; byte_index < 2U; ++byte_index)
        {
            const uint32_t byte_address = halfword_address + byte_index;
            if ((byte_address >= address) && (byte_address < end_address))
            {
                const uint16_t mask = (uint16_t)(UINT16_C(0x00FF) << (byte_index * 8U));
                const uint16_t value = (uint16_t)((uint16_t)buffer[byte_address - address]
                                                  << (byte_index * 8U));
                new_value = (uint16_t)((new_value & (uint16_t)~mask) | value);
            }
        }

        /* Flash 只能把 1 写成 0；需要 0->1 时必须先擦除所在页。 */
        if ((old_value & new_value) != new_value)
        {
            status = HAL_ERROR;
            break;
        }

        if ((new_value != old_value) &&
            (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                               halfword_address,
                               new_value) != HAL_OK))
        {
            status = HAL_ERROR;
            break;
        }

        if (*((const uint16_t *)halfword_address) != new_value)
        {
            status = HAL_ERROR;
            break;
        }
    }

    HAL_FLASH_Lock();
    return status == HAL_OK ? (int)size : -1;
}

static int cooling_flash_erase(long offset, size_t size)
{
    uint32_t address;
    uint32_t page_count;
    uint32_t page_error = UINT32_MAX;
    FLASH_EraseInitTypeDef erase = {0};
    HAL_StatusTypeDef status;

    if ((offset < 0) || (size == 0U))
    {
        return size == 0U ? 0 : -1;
    }

    address = COOLING_FLASH_BASE_ADDRESS + (uint32_t)offset;
    if (!cooling_flash_write_allowed(address, size) ||
        ((address % COOLING_FLASH_PAGE_SIZE) != 0U))
    {
        return -1;
    }

    page_count = ((uint32_t)size + COOLING_FLASH_PAGE_SIZE - 1U) /
                 COOLING_FLASH_PAGE_SIZE;
    if (page_count > ((COOLING_FLASH_END_ADDRESS - address) /
                      COOLING_FLASH_PAGE_SIZE))
    {
        return -1;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = address;
    erase.NbPages = page_count;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    status = HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();

    if ((status != HAL_OK) || (page_error != UINT32_MAX))
    {
        return -1;
    }

    return (int)size;
}

const struct fal_flash_dev cooling_onchip_flash =
{
    .name = COOLING_FAL_FLASH_NAME,
    .addr = COOLING_FLASH_BASE_ADDRESS,
    .len = COOLING_FLASH_TOTAL_SIZE,
    .blk_size = COOLING_FLASH_PAGE_SIZE,
    .ops =
    {
        cooling_flash_init,
        cooling_flash_read,
        cooling_flash_write,
        cooling_flash_erase
    },
    .write_gran = 16U
};
