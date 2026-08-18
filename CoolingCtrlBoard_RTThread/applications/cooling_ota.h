/*
 * 应用层扩展帧 OTA 服务入口
 *
 * 本服务替代早期基于标准 ID 的 common/fota_can 方案：common/fota_can
 * 文件保留给 Bootloader 使用，本应用不再启动或查询它。
 *
 * 实现为“独立工作线程 + 消息队列”模型：CAN 订阅回调只做基本帧来源/
 * 格式验证并把 can_frame_t 复制入队，Flash 擦写、元数据写入、CRC16
 * 增量累计、两级校验与复位全部在 OTA 工作线程执行；帧间超时也由
 * 工作线程带超时接收队列实现，不再需要独立监控线程，也禁止在
 * CAN 回调中执行任何 Flash 重操作或长延时。
 */

#ifndef COOLING_OTA_H
#define COOLING_OTA_H

#include <rtthread.h>

/*
 * 启动扩展帧 OTA 接收：必须在 CAN 总线启动后调用。初始化 FAL、
 * 查找 download 分区，创建 OTA 消息队列与工作线程，并依次订阅
 * OTA 数据帧（类别 1）与 OTA 请求（0x2210，精确匹配）。
 * 重复调用幂等；任何失败路径都安全返回，不会启动半套可接受请求
 * 的 OTA，也不会改变安全模块的 OTA 联锁默认状态。
 */
rt_err_t cooling_ota_start(void);

/*
 * 查询 OTA 是否处于接收窗口（请求置忙 -> 失败/超时清除或复位前保持）。
 * 置忙期间统一安全模块 OTA 联锁生效，加热模块不会开启加热，保证
 * 软件 PWM 不会在 Flash 擦写期间重新开启。本函数只读取 volatile
 * 忙标志，不取互斥量，可在任何线程安全调用。
 */
rt_bool_t cooling_ota_is_busy(void);

#endif /* COOLING_OTA_H */
