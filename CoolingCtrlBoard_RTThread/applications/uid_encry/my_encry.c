/*
 * 正点原子脱机下载器 UID 加密功能的板级接入层。
 *
 * 原示例面向 STM32F1，并把密钥放在 0x0807C000。当前工程改为 STM32F302CCT6：
 * 芯片 UID 使用 CMSIS 提供的 UID_BASE，12 字节密钥固定放在 Bootloader 末页页首。
 */

#include "my_encry.h"

#include <string.h>

#include "UID_Encryption.h"
#include "cooling_flash_layout.h"
#include "stm32f3xx.h"

uint8_t is_encry_ok = 0U;

/*
 * 保留原示例的自定义 ID 混淆方式。运行时还原后的实际 ID 为：
 * 24 57 95 02 52 24 A6 C2 F1 F0 CE AD
 */
static const uint8_t encoded_custom_id[12] =
{
    0x12U, 0x36U, 0x95U, 0x23U,
    0x52U, 0x03U, 0xA6U, 0xC3U,
    0xF2U, 0xF9U, 0xCDU, 0xADU
};

static void uid_encry_build_custom_id(uint8_t custom_id[12])
{
    memcpy(custom_id, encoded_custom_id, sizeof(encoded_custom_id));
    custom_id[0] = (uint8_t)(custom_id[0] + custom_id[0]);
    custom_id[1] = (uint8_t)(custom_id[1] + 0x21U);
    custom_id[3] = (uint8_t)(custom_id[3] - 0x21U);
    custom_id[5] = (uint8_t)(custom_id[5] + 0x21U);
    custom_id[7] = (uint8_t)(custom_id[7] - 0x01U);
    custom_id[8] = (uint8_t)(custom_id[8] - 0x01U);
    custom_id[9] = (uint8_t)(custom_id[9] - 0x09U);
    custom_id[10] = (uint8_t)(custom_id[10] + 0x01U);
}

void my_encry_init(void)
{
#if STM32_UID_ENCRY
    uint8_t custom_id[12];
    is_encry_ok = 0U;
    uid_encry_build_custom_id(custom_id);

    /* 本项目固定使用正点原子算法 3、大端序、12 字节密钥。 */
    is_encry_ok = (UID_Encryption_Key_Check(
            (void *)COOLING_UID_KEY_START_ADDRESS,
            (void *)UID_BASE,
            custom_id,
            LENGTH_12,
            BIG_ENDIA,
            ALGORITHM_3) == 0) ? 1U : 0U;
#else
    is_encry_ok = 1U;
#endif
}
