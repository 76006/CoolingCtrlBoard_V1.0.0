# CAN 通信结构

当前自研 CAN 代码只保留两个核心模块：

```text
applications/can/cooling_can.c/.h   App 协议、命令处理、状态上报
common/can/can_bus.c/.h             固定 can1 的设备收发和接收线程
```

升级逻辑独立保留：

```text
applications/ota/cooling_ota.c/.h   App 29 位扩展帧 OTA
common/fota_can.c/.h                 Bootloader 标准帧恢复 OTA
```

## 接收路径

```text
CAN 中断
  → RT-Thread CAN RX FIFO
  → can_bus_rx_notify()
  → rx_sem
  → can_bus_rx_entry()
  → 当前固件唯一接收函数
     App：can_receive()
       ├─ 普通命令直接处理
       └─ cooling_ota_receive() → otaq → ota_worker_entry()
     Bootloader：fota_can_receive()
```

接收函数运行在 `can_rx` 线程，不在中断上下文。App OTA 接收入口只把
帧放入 `otaq`，Flash 擦写由 `otawork` 线程完成。

## 发送路径

```text
cooling_can / cooling_ota / fota_can
  → can_bus_send()
  → can_tx mutex（App）
  → rt_device_write()
  → RT-Thread CAN TX mailbox
```

## App 协议

App 使用 29 位扩展 ID：

```text
bits 28..24  source
bits 23..19  target
bits 18..16  frame class
bits 15..0   function 或 OTA sequence
```

节点与主要功能码定义在 `applications/can/cooling_can.h`：

- 主控节点：`COOLING_CAN_MASTER_NODE_ID = 0x04`
- 本机节点：`COOLING_CAN_LOCAL_NODE_ID = 0x08`
- `0x1000`：上/下电广播
- `0x2000`：握手请求
- `0x2010`：自动加热控制
- `0x2100`：握手应答
- `0x2110`：周期状态
- `0x2210`：OTA 请求/准备应答
- `0x2220`：OTA 完成
- class 1：OTA 数据帧，低 16 位为帧序号

`cooling_can_start()` 启动 `can1`、设置唯一接收函数 `can_receive()`，并创建
`canstatus` 线程。收到 power/auto 命令后直接更新 `cooling_safety`，
关闭命令同时立即关闭加热；状态帧从 `cooling_safety`、
`cooling_pressure` 和 `cooling_adc` 读取数据。

## Bootloader 协议

Bootloader 使用独立的标准帧恢复协议：

- 控制：`0x6A0`
- 数据：`0x6A1`
- 应答：`0x6A8`

定义和实现位于 `common/fota_can.c/.h`，它与 App 扩展帧 OTA 共用
`can_bus.c/.h`，但不依赖 `cooling_can.c/.h`。
