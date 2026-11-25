/*
 * CoolingCtrlBoard FAL 设备与 Flash 分区配置
 *
 * 将内部 Flash 注册为 FAL 设备，并按照 cooling_flash_layout.h 建立
 * Bootloader、App、下载区、双页 OTA 元数据和掉电参数分区。App 与
 * Bootloader 共用本表，确保两边对地址和大小的理解完全一致。
 */

#ifndef COOLING_FAL_CFG_H
#define COOLING_FAL_CFG_H

#include <rtconfig.h>

#include "cooling_flash_layout.h"

extern const struct fal_flash_dev cooling_onchip_flash;

#define FAL_FLASH_DEV_TABLE                 \
{                                           \
    &cooling_onchip_flash,                  \
}

#ifdef FAL_PART_HAS_TABLE_CFG
#define FAL_PART_TABLE                                                               \
{                                                                                    \
    {FAL_PART_MAGIC_WORD, COOLING_FAL_BL_PART_NAME,       COOLING_FAL_FLASH_NAME,    \
     COOLING_BL_OFFSET,       COOLING_BL_SIZE,       0},                             \
    {FAL_PART_MAGIC_WORD, COOLING_FAL_APP_PART_NAME,      COOLING_FAL_FLASH_NAME,    \
     COOLING_APP_OFFSET,      COOLING_APP_SIZE,      0},                             \
    {FAL_PART_MAGIC_WORD, COOLING_FAL_DOWNLOAD_PART_NAME, COOLING_FAL_FLASH_NAME,    \
     COOLING_DOWNLOAD_OFFSET, COOLING_DOWNLOAD_SIZE, 0},                             \
    {FAL_PART_MAGIC_WORD, COOLING_FAL_OTA_META_PART_NAME, COOLING_FAL_FLASH_NAME,    \
     COOLING_OTA_META_OFFSET, COOLING_OTA_META_SIZE, 0},                             \
    {FAL_PART_MAGIC_WORD, COOLING_FAL_NV_PARAM_PART_NAME, COOLING_FAL_FLASH_NAME,    \
     COOLING_NV_PARAM_OFFSET, COOLING_NV_PARAM_SIZE, 0},                             \
}
#endif

#endif /* COOLING_FAL_CFG_H */
