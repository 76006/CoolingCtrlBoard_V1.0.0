/*
 * 应用层 CAN 业务服务入口。
 *
 * cooling_can_service_start() 启动 can1 统一总线、注册一次业务帧
 * 订阅并创建周期状态线程；重复调用幂等，任何失败路径都安全返回，
 * 不会创建半初始化的服务。
 */

#ifndef APPLICATIONS_COOLING_CAN_SERVICE_H
#define APPLICATIONS_COOLING_CAN_SERVICE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 can1 的唯一总线管理器；重复调用不会重复创建设备和线程。 */
rt_err_t cooling_can_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_COOLING_CAN_SERVICE_H */
