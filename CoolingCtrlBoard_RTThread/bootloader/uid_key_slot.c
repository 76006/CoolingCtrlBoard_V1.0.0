/*
 * 正点原子脱机下载器 UID 密钥占位区。
 *
 * 下载器在烧录前将下面的 12 个 0xFF 替换为根据芯片 UID 计算出的密钥。
 * 本文件只负责让密钥槽稳定存在于 Bootloader BIN，不在运行时擦写 Flash。
 */

#include <stdint.h>

#include "cooling_flash_layout.h"

#if defined(__GNUC__)
__attribute__((section(".uid_key"), used, aligned(4)))
#endif
const uint8_t cooling_uid_key_slot[COOLING_UID_KEY_SIZE] =
{
    0xFFU, 0xFFU, 0xFFU, 0xFFU,
    0xFFU, 0xFFU, 0xFFU, 0xFFU,
    0xFFU, 0xFFU, 0xFFU, 0xFFU
};
