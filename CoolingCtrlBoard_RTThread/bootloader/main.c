/*
 * CoolingCtrlBoard Bootloader 固件入口
 *
 * 先尝试安装 READY/COPYING 状态的 RBL 固件，再保留短暂 CAN 恢复窗口；
 * 若没有升级活动且 App 向量表有效，则跳转到 0x08008000 的 App。该工程
 * 只承担升级恢复和启动职责，不运行制冷控制、ADC 或周期状态任务。
 */

#include <board.h>
#include <rtthread.h>

#include "boot_fota.h"
#include "can_bus.h"
#include "fota_can.h"

#define BOOT_RECOVERY_WINDOW_MS 1500U

static void boot_ota_prepare(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
}

static void boot_ota_completed(void)
{
    NVIC_SystemReset();
}

int main(void)
{
    static const can_bus_config_t bus_config =
    {
        "can1",
        FOTA_CAN_DEFAULT_BAUD_RATE,
        2048U,
        18U,
        10U
    };
    static const fota_can_config_t can_config =
    {
        FOTA_CAN_DEFAULT_CTRL_ID,
        FOTA_CAN_DEFAULT_DATA_ID,
        FOTA_CAN_DEFAULT_RESPONSE_ID,
        boot_ota_prepare,
        boot_ota_completed
    };

    /* READY 或 COPYING 都会在这里安装；COPYING 表示上次复制中掉过电。 */
    (void)boot_fota_install_pending();
    if (can_bus_start(&bus_config) == 0)
    {
        (void)fota_can_start(&can_config);
    }

    if (boot_fota_app_is_valid())
    {
        /* 给维护工具一个短暂接管窗口；收到 BEGIN 后接收线程会保持在 Boot。 */
        rt_thread_mdelay(BOOT_RECOVERY_WINDOW_MS);
        if (!fota_can_is_receiving())
        {
            boot_fota_jump_to_app();
        }
    }

    /* App 无效或正在接收升级包时始终停留在 CAN 恢复模式。 */
    while (1)
    {
        rt_thread_mdelay(100U);
        if (boot_fota_app_is_valid() && !fota_can_is_receiving() &&
            fota_can_activity_seen())
        {
            /* ABORT 后允许已有有效 App 继续启动。 */
            boot_fota_jump_to_app();
        }
    }
}
