/*
 * CoolingCtrlBoard Bootloader FOTA 安装与跳转接口
 *
 * 提供待升级固件安装、App 向量表有效性检查以及从 Bootloader 跳转
 * 到 App 的入口。具体 Flash 擦写、掉电恢复和完整性校验在 boot_fota.c。
 */

#ifndef BOOT_FOTA_H
#define BOOT_FOTA_H

int boot_fota_install_pending(void);
int boot_fota_app_is_valid(void);
void boot_fota_jump_to_app(void);

#endif /* BOOT_FOTA_H */
