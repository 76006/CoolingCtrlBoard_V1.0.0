/*
 * 正点原子 UID 自定义加密算法接口
 *
 * 本产品只保留下载器兼容的算法 3、大端序和 12 字节密钥组合，用于
 * 判断密钥是否与 MCU UID 匹配。接口保持与原下载器算法的参数形式
 * 兼容，但不再暴露本项目不会使用的其他算法和端序选项。
 */

#ifndef APPLICATIONS_UID_ENCRY_UID_ENCRYPTION_H
#define APPLICATIONS_UID_ENCRY_UID_ENCRYPTION_H

#include <stdint.h>

typedef enum
{
    LENGTH_12 = 12
} eKeyLengthType;

typedef enum
{
    BIG_ENDIA = 1
} eEndiaType;

typedef enum
{
    ALGORITHM_3 = 3
} eAlgorithmType;

/* 返回 0 表示密钥匹配，返回 1 表示参数或密钥不匹配。 */
char UID_Encryption_Key_Check(void *pKey,
                              void *pUID,
                              void *pCustomID,
                              eKeyLengthType keyLength,
                              eEndiaType endiaType,
                              eAlgorithmType algorithmNum);

/* 供产测调试使用：按固定配置计算 12 字节密钥。 */
void UID_Encryption_Key_Calculate(void *pKey,
                                  void *pUID,
                                  void *pCustomID,
                                  eKeyLengthType keyLength,
                                  eEndiaType endiaType,
                                  eAlgorithmType algorithmNum);

#endif /* APPLICATIONS_UID_ENCRY_UID_ENCRYPTION_H */
