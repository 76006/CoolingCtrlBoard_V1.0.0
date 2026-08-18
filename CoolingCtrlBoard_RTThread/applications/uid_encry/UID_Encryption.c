/*
 * 正点原子脱机下载器 UID 自定义加密算法的精简移植。
 *
 * 根据 STM32 的 96 位 UID 和固定算法生成期望密钥，再与 Bootloader
 * 密钥槽中的 12 字节内容比较。本产品只保留下载器 V1.2 的算法 3、
 * 大端序组合，删除其余算法分支以降低代码体积和移植复杂度。
 */

#include "UID_Encryption.h"

#include <stddef.h>
#include <string.h>

#define UID_ENCRY_KEY_SIZE 12U

static int uid_encry_parameters_valid(const void *key,
                                      const void *uid,
                                      const void *custom_id,
                                      eKeyLengthType key_length,
                                      eEndiaType endian,
                                      eAlgorithmType algorithm)
{
    return (key != NULL) && (uid != NULL) && (custom_id != NULL) &&
           (key_length == LENGTH_12) && (endian == BIG_ENDIA) &&
           (algorithm == ALGORITHM_3);
}

static void uid_encry_algorithm_3(const uint8_t custom_id[UID_ENCRY_KEY_SIZE],
                                  const uint8_t uid[UID_ENCRY_KEY_SIZE],
                                  uint8_t key[UID_ENCRY_KEY_SIZE])
{
    key[0] = (uint8_t)(((uid[0] & custom_id[1]) ^ custom_id[2]) ^ custom_id[3]);
    key[1] = (uint8_t)((uid[1] ^ (custom_id[2] & custom_id[3])) ^ custom_id[4]);
    key[2] = (uint8_t)((uid[2] | (custom_id[3] ^ custom_id[4])) | custom_id[5]);
    key[3] = (uint8_t)((uid[3] ^ custom_id[4]) | (custom_id[5] ^ custom_id[6]));
    key[4] = (uint8_t)((custom_id[4] ^ (uid[5] & custom_id[6])) ^ custom_id[7]);
    key[5] = (uint8_t)(custom_id[5] | (uid[6] ^ (custom_id[7] & custom_id[8])));
    key[6] = (uint8_t)(((custom_id[6] & uid[7]) ^ custom_id[6]) | custom_id[9]);
    key[7] = (uint8_t)((custom_id[7] ^ uid[8]) | (custom_id[9] ^ custom_id[10]));
    key[8] = (uint8_t)((custom_id[8] ^ (custom_id[9] & uid[10])) ^ custom_id[11]);
    key[9] = (uint8_t)((custom_id[9] ^ custom_id[10]) | (uid[11] & custom_id[0]));
    key[10] = (uint8_t)(((custom_id[10] & custom_id[11]) ^ uid[0]) ^ custom_id[1]);
    key[11] = (uint8_t)((custom_id[11] ^ (custom_id[0] & uid[1])) ^ custom_id[2]);
}

/* 大端配置等价于原 V1.2 代码对每个 32 位字节序翻转。 */
static void uid_encry_to_big_endian(uint8_t key[UID_ENCRY_KEY_SIZE])
{
    unsigned int word;

    for (word = 0U; word < (UID_ENCRY_KEY_SIZE / 4U); ++word)
    {
        const unsigned int base = word * 4U;
        uint8_t temporary;

        temporary = key[base];
        key[base] = key[base + 3U];
        key[base + 3U] = temporary;

        temporary = key[base + 1U];
        key[base + 1U] = key[base + 2U];
        key[base + 2U] = temporary;
    }
}

static void uid_encry_calculate_fixed(uint8_t key[UID_ENCRY_KEY_SIZE],
                                      const uint8_t uid[UID_ENCRY_KEY_SIZE],
                                      const uint8_t custom_id[UID_ENCRY_KEY_SIZE])
{
    uid_encry_algorithm_3(custom_id, uid, key);
    uid_encry_to_big_endian(key);
}

char UID_Encryption_Key_Check(void *pKey,
                              void *pUID,
                              void *pCustomID,
                              eKeyLengthType keyLength,
                              eEndiaType endiaType,
                              eAlgorithmType algorithmNum)
{
    uint8_t calculated_key[UID_ENCRY_KEY_SIZE];

    if (!uid_encry_parameters_valid(pKey, pUID, pCustomID, keyLength,
                                    endiaType, algorithmNum))
    {
        return 1;
    }

    uid_encry_calculate_fixed(calculated_key,
                              (const uint8_t *)pUID,
                              (const uint8_t *)pCustomID);
    return memcmp(pKey, calculated_key, sizeof(calculated_key)) == 0 ? 0 : 1;
}

void UID_Encryption_Key_Calculate(void *pKey,
                                  void *pUID,
                                  void *pCustomID,
                                  eKeyLengthType keyLength,
                                  eEndiaType endiaType,
                                  eAlgorithmType algorithmNum)
{
    if (!uid_encry_parameters_valid(pKey, pUID, pCustomID, keyLength,
                                    endiaType, algorithmNum))
    {
        return;
    }

    uid_encry_calculate_fixed((uint8_t *)pKey,
                              (const uint8_t *)pUID,
                              (const uint8_t *)pCustomID);
}
