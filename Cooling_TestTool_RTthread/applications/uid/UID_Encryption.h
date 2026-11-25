/*
 * 正点原子 UID 自定义加密算法接口
 *
 * 本产品只保留下载器兼容的算法 3、大端序和 12 字节密钥组合。
 */

#ifndef APPLICATIONS_UID_UID_ENCRYPTION_H
#define APPLICATIONS_UID_UID_ENCRYPTION_H

#include <stdint.h>

/* 返回 0 表示密钥匹配，返回 1 表示不匹配。 */
char UID_Encryption_Key_Check(const void *key,
                              const void *uid,
                              const void *custom_id);

#endif /* APPLICATIONS_UID_UID_ENCRYPTION_H */
