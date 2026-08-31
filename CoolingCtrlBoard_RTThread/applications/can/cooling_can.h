/* CoolingCtrlBoard App CAN 协议与入口。 */

#ifndef APPLICATIONS_COOLING_CAN_H
#define APPLICATIONS_COOLING_CAN_H

#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COOLING_CAN_NODE_BROADCAST        0x00U
#define COOLING_CAN_LOCAL_NODE_ID         0x08U
#define COOLING_CAN_MASTER_NODE_ID        0x04U

#define COOLING_CAN_CLASS_NORMAL          0U
#define COOLING_CAN_CLASS_OTA_DATA        1U

#define COOLING_CAN_FUNC_POWER_BROADCAST  0x1000U
#define COOLING_CAN_FUNC_HANDSHAKE_REQ    0x2000U
#define COOLING_CAN_FUNC_AUTO_HEAT        0x2010U
#define COOLING_CAN_FUNC_HANDSHAKE_REPLY  0x2100U
#define COOLING_CAN_FUNC_STATUS           0x2110U
#define COOLING_CAN_FUNC_OTA_REQUEST      0x2210U
#define COOLING_CAN_FUNC_OTA_COMPLETE     0x2220U

#define COOLING_CAN_DLC_POWER_BROADCAST   4U
#define COOLING_CAN_DLC_HANDSHAKE         8U
#define COOLING_CAN_DLC_AUTO_HEAT         2U
#define COOLING_CAN_DLC_STATUS            8U
#define COOLING_CAN_DLC_OTA_REQUEST       8U
#define COOLING_CAN_DLC_OTA_ACK           2U
#define COOLING_CAN_DLC_OTA_COMPLETE      4U

#define COOLING_CAN_POWER_ON              1U
#define COOLING_CAN_POWER_OFF             0U
#define COOLING_CAN_AUTO_HEAT_ON          1U
#define COOLING_CAN_AUTO_HEAT_OFF         0U

#define COOLING_CAN_OTA_RESULT_OK         1U
#define COOLING_CAN_OTA_RESULT_FAIL       0U

typedef enum
{
    COOLING_CAN_OTA_FAIL_CRC16 = 0,
    COOLING_CAN_OTA_FAIL_TIMEOUT = 1,
    COOLING_CAN_OTA_FAIL_OTHER = 2
} cooling_can_ota_fail_reason_t;

#define COOLING_CAN_REFRIGERANT_NONE        0U
#define COOLING_CAN_REFRIGERANT_SUFFICIENT  100U
#define COOLING_CAN_REFRIGERANT_PREHEATING  255U

#define COOLING_CAN_OTA_FRAME_DATA_SIZE    8U

#define COOLING_CAN_ID(source, target, frame_class, value) \
    (((uint32_t)(source) << 24U) | ((uint32_t)(target) << 19U) | \
     ((uint32_t)(frame_class) << 16U) | (uint16_t)(value))
#define COOLING_CAN_ID_SOURCE(id)  ((uint8_t)((id) >> 24U))
#define COOLING_CAN_ID_TARGET(id)  ((uint8_t)(((id) >> 19U) & 0x1FU))
#define COOLING_CAN_ID_CLASS(id)   ((uint8_t)(((id) >> 16U) & 0x07U))
#define COOLING_CAN_ID_VALUE(id)   ((uint16_t)(id))

rt_err_t cooling_can_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_CAN_H */
