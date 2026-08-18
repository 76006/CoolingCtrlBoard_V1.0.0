/*
 * CoolingCtrlBoard CAN 业务服务配置
 *
 * 只保存 CAN 总线、节点地址、协议版本和状态上报任务等通信层参数。
 * OTA 服务资源见 cooling_ota_config.h，传感器通道与标定参数见
 * cooling_sensor_config.h，避免通信配置承担其他模块的职责。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_CAN_CONFIG_H
#define APPLICATIONS_COOLING_CAN_CONFIG_H

#include "cooling_can_protocol.h"

/* CAN 设备与波特率。 */
#define COOLING_CAN_DEVICE_NAME             "can1"
#define COOLING_CAN_BAUD_RATE               500000U

/* CAN 接收线程参数。OTA 的 Flash 擦写/校验已移入工作线程，
 * 本线程只做轻量分发与命令转发。 */
#define COOLING_CAN_RX_THREAD_STACK_SIZE    2048U
#define COOLING_CAN_RX_THREAD_PRIORITY      18U
#define COOLING_CAN_RX_THREAD_TIMESLICE     10U

/* 节点地址：广播 0x00、主控 0x04、制冷 0x08。 */
#define COOLING_CAN_LOCAL_NODE_ID           0x08U
#define COOLING_CAN_MASTER_NODE_ID          0x04U

/*
 * 设备代码与版本。版本按十进制数值编码（不是 BCD/十六进制半字节）：
 * version = major*10000 + minor*100 + patch。
 * 1.0.0 = 10000（0x2710），1.1.1 = 10101。
 */
#define COOLING_CAN_DEVICE_CODE             8U
#define COOLING_CAN_PROTOCOL_VERSION        10000U
#define COOLING_CAN_SOFTWARE_VERSION        10000U
#define COOLING_CAN_HARDWARE_VERSION        10000U

/* 状态上报周期与线程参数。上电后按该周期发送 0x2110。 */
#define COOLING_CAN_STATUS_PERIOD_MS        2000U
#define COOLING_CAN_STATUS_THREAD_STACK_SIZE 1024U
#define COOLING_CAN_STATUS_THREAD_PRIORITY  17U
#define COOLING_CAN_STATUS_THREAD_TIMESLICE 10U

/* ---------------------------------------------------------------------
 * 编译期范围检查
 * --------------------------------------------------------------------- */

#if (COOLING_CAN_LOCAL_NODE_ID == COOLING_CAN_NODE_BROADCAST) || \
    (COOLING_CAN_LOCAL_NODE_ID > COOLING_CAN_NODE_MAX)
#error "COOLING_CAN_LOCAL_NODE_ID must be a valid non-broadcast node (1..31)"
#endif

#if (COOLING_CAN_MASTER_NODE_ID == COOLING_CAN_NODE_BROADCAST) || \
    (COOLING_CAN_MASTER_NODE_ID > COOLING_CAN_NODE_MAX) || \
    (COOLING_CAN_MASTER_NODE_ID == COOLING_CAN_LOCAL_NODE_ID)
#error "COOLING_CAN_MASTER_NODE_ID must be a valid node different from the local node"
#endif

#if (COOLING_CAN_DEVICE_CODE > 0xFFFFU)
#error "COOLING_CAN_DEVICE_CODE must fit in 16 bits"
#endif

#if (COOLING_CAN_PROTOCOL_VERSION > 0xFFFFU) || \
    (COOLING_CAN_SOFTWARE_VERSION > 0xFFFFU) || \
    (COOLING_CAN_HARDWARE_VERSION > 0xFFFFU)
#error "version codes are decimal major*10000+minor*100+patch and must fit in 16 bits"
#endif

#if (COOLING_CAN_STATUS_PERIOD_MS == 0U)
#error "COOLING_CAN_STATUS_PERIOD_MS must be positive"
#endif

#if (COOLING_CAN_STATUS_THREAD_STACK_SIZE == 0U)
#error "status thread stack size must be positive"
#endif

#endif /* APPLICATIONS_COOLING_CAN_CONFIG_H */
