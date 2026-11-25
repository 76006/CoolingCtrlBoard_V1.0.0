/*
 * CAN 经典帧 OTA 接收服务。
 * 协议层只接收完整 RBL 文件，只有文件 CRC 和 RBL 内部校验均通过后才置 READY。
 */

#ifndef FOTA_CAN_H
#define FOTA_CAN_H

#include <stdint.h>

#define FOTA_CAN_DEFAULT_CTRL_ID       UINT32_C(0x6A0)
#define FOTA_CAN_DEFAULT_DATA_ID       UINT32_C(0x6A1)
#define FOTA_CAN_DEFAULT_RESPONSE_ID   UINT32_C(0x6A8)
#define FOTA_CAN_BLOCK_SIZE            256U

typedef struct
{
    uint32_t control_id;
    uint32_t data_id;
    uint32_t response_id;
    void (*prepare)(void);
    void (*completed)(void);
} fota_can_config_t;

int fota_can_start(const fota_can_config_t *config);
int fota_can_activity_seen(void);
int fota_can_is_receiving(void);

#endif /* FOTA_CAN_H */
