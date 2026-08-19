/*
 * CoolingCtrlBoard 应用层扩展帧 OTA 服务入口
 *
 * 本服务替代早期基于标准 ID 的 common/fota_can 方案：common/fota_can
 * 文件保留给 Bootloader 使用，本应用不再启动或查询它。
 *
 * 实现为“独立工作线程 + 消息队列”模型：CAN 接收入口把 can_frame_t
 * 复制入队，Flash 擦写、元数据写入、CRC16
 * 增量累计、两级校验与复位全部在 OTA 工作线程执行；帧间超时也由
 * 工作线程带超时接收队列实现，不再需要独立监控线程，也禁止在
 * CAN 回调中执行任何 Flash 重操作或长延时。
 */

#ifndef COOLING_OTA_H
#define COOLING_OTA_H

#include <rtthread.h>
#include "can/can_bus.h"

/*
 * 初始化 FAL、download 分区、消息队列和工作线程。
 */
rt_err_t cooling_ota_start(void);

/* 由 cooling_can 的唯一接收入口调用，只负责把 OTA 帧放入工作队列。 */
void cooling_ota_receive(const can_frame_t *frame);

#endif /* COOLING_OTA_H */
