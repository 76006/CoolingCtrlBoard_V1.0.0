# CoolingCtrlBoard RT-Thread Studio 使用说明

本工程面向自定义 CoolingCtrlBoard V1.0，不是 Nucleo 开发板。目标芯片为 STM32F302CCT6，RT-Thread 版本为 v5.2.2。

可直接导入的完整工程位于：

```text
D:\workspace\CoolingCtrlBoard_V1.0.0\CoolingCtrlBoard_RTThread
```

工程已经包含 RT-Thread 内核、STM32F3 CMSIS、HAL 和板级驱动，正常导入和编译不需要再下载 SDK，也不要从 Studio 的“新建芯片工程”入口重新选芯片。

## 1. 导入 RT-Thread Studio

1. 打开 RT-Thread Studio。
2. 选择 `File` -> `Import...`。
3. 选择 `General` -> `Existing Projects into Workspace`，然后点 `Next`。
4. 在 `Select root directory` 中选择：
   `D:\workspace\CoolingCtrlBoard_V1.0.0\CoolingCtrlBoard_RTThread`
5. 列表中应出现项目 `CoolingCtrlBoard`，勾选它。
6. 建议不要勾选 `Copy projects into workspace`，直接使用当前目录，避免形成两份工程。
7. 点击 `Finish`。

如果工作区中已经存在同名项目，先在 Studio 中将旧项目从工作区移除，但不要勾选删除磁盘文件，然后重新导入。

## 2. 编译

导入后右键项目 `CoolingCtrlBoard`，选择 `Build Project`，或点击工具栏的锤子按钮。

当前机器上的 RT-Thread Studio 内置 `GNU Tools for ARM Embedded Processors 5.4.1` 已经实际完整编译通过，不需要切换到 CubeIDE 编译器。

两种构建输出位置可能不同：

- Studio 托管构建：`CoolingCtrlBoard_RTThread\Debug\rtthread.elf` 和 `rtthread.bin`
- Studio SCons 构建：`CoolingCtrlBoard_RTThread\rt-thread.elf` 和 `rtthread.bin`

两种构建方式均已配置为体积优化 `-Os`，以确保 App 能放入 OTA 的 106 KiB 分区。修改构建配置后，请先执行一次 `Project` -> `Clean...`，再重新 `Build Project`，避免继续链接旧的 `-O0` 目标文件。

### 正点原子下载器 UID 密钥

正点原子 UID 自定义加密 V1.2 已接入 App。12 字节密钥位于 Bootloader 最后一个独立 Flash 页的页首：

```text
Flash 地址：0x08007800
Bootloader BIN 内偏移：0x00007800
密钥长度：12 字节
算法配置：算法 3，大端序
```

`cooling_bootloader.bin` 在该位置预置 12 个 `0xFF`，下载器首次烧录前直接将计算出的密钥覆盖到这里。该页不属于 App、下载区或参数区，后续 RBL OTA 不会擦除密钥。

Bootloader 和 App 都会在板级初始化最前端校验密钥。校验失败时，串口、CAN、ADC、FinSH 和所有业务线程都不会初始化；串口/CAN 引脚恢复为模拟输入，加热输出保持关闭，系统静默锁定。此状态只能通过下载器重新烧录正确密钥或完整固件恢复。

## 2.1 ADC、开关任务和 CAN 取值接口

应用启动后会创建两个后台任务：

- ADC 任务每 100 ms 依次采集 `NTC_ADC0`、`NTC_ADC1` 和 `HYSN_ADC2`。每路先经过 5 点中值滤波去除孤立毛刺，再经过系数为 1/4 的整数 IIR 滤波。
- 开关任务每 10 ms 读取一次 PB15，连续 5 次相同后才更新稳定值，因此消抖时间约为 50 ms。
- PB0 加热输出始终默认关闭。任务不会自动开启加热，只有应用显式调用 `cooling_heater_set(RT_TRUE)` 才会拉高；调用方必须自行实现温度、限位和超时联锁。

代码按以下层次组织：

```text
main.c                         启动入口
  ├─ cooling_adc.c             ADC 任务、快照发布
  │    └─ cooling_filter.c     纯 C 中值/IIR 算法
  └─ cooling_switch.c          开关消抖、输出状态
       ↓
cooling_board_port.c           RT-Thread ADC/GPIO 板级适配
       ↓
board.h + RT-Thread BSP        STM32 引脚、通道和底层驱动
```

采样周期、滤波参数、线程栈和优先级统一放在 `applications/cooling_config.h`。业务通道定义放在 `applications/cooling_channels.h`。如果移植到另一块板，优先修改 `board.h`、`cooling_board_port.c` 和 `cooling_config.h`；ADC/开关服务及未来 CAN 代码可以保持不变。

ADC 的公共数据结构和接口位于 `applications/cooling_adc.h`：

```c
cooling_adc_snapshot_t adc;

if (cooling_adc_get_snapshot(&adc) == RT_EOK &&
    adc.valid_mask == COOLING_ADC_VALID_ALL)
{
    /* adc.value[COOLING_ADC_NTC0].filtered   - 滤波后的 12 位 ADC 值 */
    /* adc.value[COOLING_ADC_NTC0].millivolts - 按 3.3 V 标称参考值换算 */
    /* 在这里逐字段装入 CAN 数据帧，不要直接发送整个结构体。 */
}
```

开关状态通过 `cooling_switch_get_snapshot()` 取得。`limit_level` 是消抖后的 PB15 电平，`limit_valid` 表示消抖结果是否已经有效，`heater_on` 是当前加热输出命令状态。

串口 MSH 中可以执行 `adc_values` 查看三路 ADC 原始值、滤波值和毫伏值，执行 `io_values` 查看限位开关与加热输出状态。当前没有加入 NTC 温度曲线，因为热敏电阻型号、分压电阻和 B 值尚未确定。

## 3. 当前硬件配置

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| MSH 控制台 | USART1 PB6/TX、PB7/RX | 115200、8-N-1、接收中断 |
| ADC1 | PA0、PA1、PA2 | 通道 1/2/3、轮询、181.5 cycles |
| CAN | PA11/RX、PA12/TX | 500 kbit/s |
| HTR_PWR | PB0 | 上电后先拉低，再配置为输出 |
| 限位开关 | PB15 | 输入、无内部上下拉 |
| SWD | PA13/SWDIO、PA14/SWCLK | 调试和下载 |

系统时钟使用外部 8 MHz 晶振，主频 72 MHz。Flash 配置为 256 KiB，SRAM 配置为 40 KiB。

## 4. 首次连接 ST-LINK

建议连接以下信号：

| ST-LINK | 板端 |
| --- | --- |
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND |
| VTref/3.3V reference | 板上 3.3V |
| NRST | NRST，建议连接 |

VTref 用于让调试器识别目标电平。是否由 ST-LINK 给板子供电取决于你的硬件设计，不确定时先让板子独立稳定供电，并确保两边共地。

在 Studio 中新建或修改下载配置时：

- 调试器选择 `ST-LINK`。
- 接口选择 `SWD`。
- 芯片填写或选择 `STM32F302CCT6` / `STM32F302CC`。
- 如果芯片列表仍没有 F302，OpenOCD 目标系列选择 `stm32f3x`；这不影响已经导入的工程编译。
- 首次连接失败时，可尝试 `Connect under reset`，并确保 NRST 已连接。

下载配置需要实际连接 ST-LINK 后才能最终验证，目前尚未对实体板烧录。

## 5. 串口检查

使用 3.3V TTL 串口模块连接：

| USB 转串口 | 板端 |
| --- | --- |
| RX | PB6 / USART1_TX |
| TX | PB7 / USART1_RX |
| GND | GND |

串口工具设置为 `115200`、`8 data bits`、`No parity`、`1 stop bit`、`No flow control`。

复位后应看到类似内容：

```text
CoolingCtrlBoard / STM32F302CCT6 / 72000000 Hz
uart1: ready
adc1: ready
can1: ready
limit switch: 0
```

最后一行会随 PB15 的实际输入电平变化，随后可以使用 MSH 命令行。

## 6. 首次上电安全要求

- PB0 是 `HTR_PWR`。当前 BSP 和示例应用只会将它保持为低电平，不会主动开启加热器。
- 首次烧录时建议断开加热功率级或使用限流电源，确认 PB0 始终为低后再接负载。
- PA0、PA1、PA2 的 ADC 输入电压必须处于 0 到 VDDA 范围内，不得输入 5V。
- PA11、PA12 是 MCU 的 CAN 逻辑信号，必须通过 CAN 收发器接入 CANH/CANL，不能直接接总线。
- CAN 总线仅在两端各放置 120 欧姆终端电阻。

## 7. 已完成的验证

- RT-Thread Studio 内置 GCC 5.4.1 托管构建：0 错误、0 警告。
- Studio SCons 构建：成功生成 ELF 和 BIN。
- STM32F302xC 启动文件、256 KiB Flash、40 KiB SRAM、Cortex-M4F hard-float 参数已检查。
- USART1、ADC1、CAN 500 kbit/s 和 PB0 安全初始化已做静态检查。

剩余工作是连接实体板，完成 ST-LINK 下载、72 MHz 时钟、串口、ADC、CAN 和限位开关的实机验证。

## 8. Bootloader/App 双目标和 OTA

工程现已包含 Bootloader/App 两个构建目标。两份 BIN、首次合并镜像、RBL 打包参数、CAN 升级协议和掉电测试步骤请查看 `OTA移植说明.md`。App 的链接地址已经改为 `0x08008000`，因此不能再将普通 `rtthread.bin` 当作地址为 `0x08000000` 的单体程序烧录。
