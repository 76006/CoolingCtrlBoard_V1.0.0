# CoolingCtrlBoard CAN OTA 移植说明

本工程已经采用“同一个 RT-Thread Studio 工程、Bootloader/App 两个构建目标”的方式完成第一版 OTA 移植。首次烧录使用两个 BIN；后续升级只传输由 App BIN 打包得到的 RBL 文件。

## 1. Flash 分区

STM32F302CCT6 的 Flash 擦除粒度是 2 KiB。所有边界都按整页对齐，擦除一个分区不会碰到相邻分区。

| 分区 | 起始地址 | 大小 | Flash 页 | 用途 |
| --- | --- | ---: | ---: | --- |
| `bl` | `0x08000000` | 32 KiB | 0–15 | Bootloader |
| `app` | `0x08008000` | 106 KiB | 16–68 | 当前运行程序 |
| `download` | `0x08022800` | 106 KiB | 69–121 | 完整 RBL 暂存区 |
| `ota_meta` | `0x0803D000` | 4 KiB | 122–123 | 两页掉电状态记录 |
| `nv_param` | `0x0803E000` | 8 KiB | 124–127 | 预留掉电参数区 |

`ota_meta` 采用两页交替记录，先写完整记录，最后写提交标志。状态写一半掉电时，Bootloader 会继续使用另一页的旧记录。`COPYING` 状态下掉电，下载区仍保存完整 RBL，下次上电从 App 起始地址重新安装，不需要旧版本回滚区。

## 2. 生成固件

在项目目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -Target All
```

也可以在 RT-Thread Studio 的 Make Targets 视图中运行：

- `firmware-app`
- `firmware-bootloader`
- `firmware-all`

输出在 `output` 文件夹：

| 文件 | 下载地址/用途 |
| --- | --- |
| `cooling_bootloader.bin` | 首次烧录到 `0x08000000` |
| `cooling_app.bin` | 首次烧录到 `0x08008000`，也是后续 RBL 的输入文件 |
| `cooling_factory.bin` | 可选的单文件首次烧录镜像，下载到 `0x08000000` |

正点原子下载器支持多地址时，选择前两个 BIN 并分别填写上述地址即可。不要把 `cooling_app.bin` 下载到 `0x08000000`。

## 3. RBL 打包器设置

使用你已有的 RT-Thread RBL 打包器：

- 输入固件：`output/cooling_app.bin`
- 目标分区名：`app`（必须完全一致，小写）
- 压缩算法：不压缩
- 加密算法：不加密
- 输出示例：`cooling_app.rbl`

当前版本只接收“不压缩、不加密”的标准 RBL。Bootloader 会检查 RBL 标识、包头 CRC、分区名、算法、大小和 App 正文 CRC。App BIN 的链接上限额外扣除了 96 字节 RBL 包头，因此生成的 RBL 也能完整放进 `download` 分区。

## 4. CAN 下载协议

默认 CAN 配置：500 kbit/s、11 位标准帧。

| CAN ID | 方向 | 用途 |
| --- | --- | --- |
| `0x6A0` | 上位机 → 板 | 控制命令 |
| `0x6A1` | 上位机 → 板 | RBL 原始数据 |
| `0x6A8` | 板 → 上位机 | 响应/进度 |

所有多字节整数均为小端序。一个数据块最大 256 字节。

### 控制帧

| 操作 | `0x6A0` 数据 |
| --- | --- |
| BEGIN | `[01][RBL总长度 u32][00][00][00]` |
| BLOCK_BEGIN | `[02][块序号 u16][块长度 u16][块CRC16 u16][00]` |
| END | `[03][整个RBL文件CRC32 u32][00][00][00]` |
| ABORT | `[04]` |
| QUERY | `[05]` |

CRC16 是 CCITT-FALSE：初值 `0xFFFF`，多项式 `0x1021`。文件 CRC32 与常用 ZIP/以太网 CRC32 一致：反射多项式 `0xEDB88320`、初值和末异或均为 `0xFFFFFFFF`。

每个 `BLOCK_BEGIN` 后紧跟若干个 `0x6A1` 数据帧，按顺序凑满该块声明的长度。最后一帧可以少于 8 字节，不得把填充字节算入块长度或 CRC。

### 响应帧

`0x6A8` 固定为：`[操作][状态][下一块序号 u16][进度/附加值 u32]`。

状态 `00` 表示成功；`01` 命令错误、`02` 状态错误、`03` 长度错误、`04` 块序号错误、`05` CRC 错误、`06` Flash 错误、`07` RBL 校验错误、`08` 文件过大。

上位机必须采用“发一个块、等 ACK、再发下一块”的方式。BEGIN 会先擦除整个下载区，耗时期间不要发数据；收到 BEGIN 成功响应后才能开始发块。ACK 丢失时可以重发同一块的 `BLOCK_BEGIN`，板端会返回当前进度，不会把已经写入的块再写一遍。

## 5. 安全与掉电行为

- App 和 Bootloader 启动时都会先将 PB0 加热输出拉低。
- 下载、块 CRC、文件 CRC 或 RBL CRC 任意失败时，都不会擦除 App。
- END 全部通过后才写 `READY` 并复位。
- Bootloader 再次完整验证下载区后才写 `COPYING`、擦除 App 并安装。
- 安装中掉电会保留 `COPYING`，下次上电从头复制并最终再次校验 App。
- 如果 RBL 本身 CRC 正确但业务逻辑错误，CRC 无法识别这种“合法但有 bug”的固件；本方案按需求不保留旧版本回滚。
- 有效 App 启动前有约 1.5 秒 CAN 维护窗口；App 无效时 Bootloader 会一直保留在 CAN 恢复模式。

## 6. 代码分层

```text
applications/cooling_ota.c    App 业务接入：关加热、完成后复位
common/fota_can.c             CAN 分块协议
common/fota_rbl.c             标准 RBL 解析和校验
common/fota_meta.c            双页掉电状态
common/fota_storage.c         FAL 分区访问适配
common/fal_flash_f302.c       STM32F302 片上 Flash 驱动
common/cooling_flash_layout.h 唯一分区定义
bootloader/boot_fota.c        安装、恢复、App 合法性和跳转
```

分区、算法和协议与具体业务分开；以后迁移到另一块 STM32 时，主要更换 Flash 驱动、链接脚本和板级 CAN/GPIO 配置。

## 7. 实机验证顺序

1. 先断开加热功率级，用下载器将 Bootloader/App 分别烧到两个地址。
2. 复位并确认 App 串口正常、PB0 始终为低。
3. 发送 QUERY，确认 CAN 响应 ID 和 500 kbit/s 参数。
4. 用一个只改变版本标识或串口提示的 App 制作 RBL，完成一次升级。
5. 分别在下载中、写状态时、安装 App 中切断电源，确认旧 App 不被提前破坏，或上电后能从下载区继续安装。

当前已完成两套镜像的编译、链接地址和合并镜像检查；真实 Flash 擦写时间、CAN 收发器及断电恢复仍需要在你的实体板上验证。
