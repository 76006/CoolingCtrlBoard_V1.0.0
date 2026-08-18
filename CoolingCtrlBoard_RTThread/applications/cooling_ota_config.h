/*
 * CoolingCtrlBoard OTA 服务配置
 *
 * 集中保存升级接收超时、消息队列和工作线程资源等 OTA 自身参数。
 * CAN 功能码、帧类别、ACK 值等线上格式仍由 cooling_can_protocol.h
 * 定义，避免把“升级服务配置”和“通信协议定义”混在一起。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_COOLING_OTA_CONFIG_H
#define APPLICATIONS_COOLING_OTA_CONFIG_H

#include "can/cooling_can_protocol.h"

/* 从准备成功或最后一个有效数据帧起，超过该时间即终止本次接收。 */
#define COOLING_OTA_FRAME_TIMEOUT_MS       1000U
#define COOLING_OTA_TIMEOUT_CHECK_MS       100U

/* OTA 工作线程消息队列容量：至少能够缓存 4 条请求/数据消息。 */
#define COOLING_OTA_QUEUE_CAPACITY         4U

/* 完成应答发送后等待该时间再复位，确保报文已经进入发送队列。 */
#define COOLING_OTA_RESET_DELAY_MS         50U

/* 当前 CAN 传输用 16 位帧序号，每帧最多携带 8 字节。 */
#define COOLING_OTA_MAX_RBL_SIZE           (512U * 1024U)

/* RBL 解析和 Flash 校验调用层次较深，给工作线程保留独立栈余量。 */
#define COOLING_OTA_THREAD_STACK_SIZE      2048U
#define COOLING_OTA_THREAD_PRIORITY        17U
#define COOLING_OTA_THREAD_TIMESLICE       10U

/* ---------------------------------------------------------------------
 * 编译期范围检查
 * --------------------------------------------------------------------- */

#if (COOLING_OTA_FRAME_TIMEOUT_MS == 0U) || \
    (COOLING_OTA_TIMEOUT_CHECK_MS == 0U) || \
    (COOLING_OTA_TIMEOUT_CHECK_MS > COOLING_OTA_FRAME_TIMEOUT_MS)
#error "invalid OTA frame timeout configuration"
#endif

#if (COOLING_OTA_QUEUE_CAPACITY < 4U)
#error "COOLING_OTA_QUEUE_CAPACITY must be at least 4"
#endif

#if (COOLING_OTA_RESET_DELAY_MS == 0U)
#error "COOLING_OTA_RESET_DELAY_MS must be positive"
#endif

#if (COOLING_OTA_MAX_RBL_SIZE != \
     ((COOLING_CAN_OTA_SEQ_MAX + 1UL) * COOLING_CAN_OTA_FRAME_DATA_SIZE))
#error "COOLING_OTA_MAX_RBL_SIZE must match the 16-bit CAN sequence range"
#endif

#if (COOLING_OTA_THREAD_STACK_SIZE == 0U)
#error "OTA worker thread stack size must be positive"
#endif

#endif /* APPLICATIONS_COOLING_OTA_CONFIG_H */
