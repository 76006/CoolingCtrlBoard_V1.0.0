/*
 * 正点原子脱机下载器 UID 授权校验的应用接口
 *
 * 读取 Bootloader 固定密钥槽中的 12 字节密钥，并与本机 STM32 UID
 * 进行匹配。校验结果发布为只读标志，供启动流程和安全联锁判断；
 * 本文件不包含具体加密算法，算法实现在 UID_Encryption.c。
 */

#ifndef APPLICATIONS_UID_COOLING_UID_AUTH_H
#define APPLICATIONS_UID_COOLING_UID_AUTH_H

#include <stdint.h>

/* 需要临时关闭授权校验时，可由构建参数显式定义为 0。 */
#ifndef STM32_UID_ENCRY
#define STM32_UID_ENCRY 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 0 表示密钥无效，1 表示密钥与本机 UID 匹配。 */
extern uint8_t is_encry_ok;

/* 读取 Bootloader 密钥槽并完成 UID 校验。 */
void my_encry_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_UID_COOLING_UID_AUTH_H */
