# CoolingCtrlBoard V1.0 BSP

This BSP targets the custom CoolingCtrlBoard V1.0 based on an
STM32F302CCT6 (LQFP48). It is derived structurally from the official
`stm32f302-st-nucleo` BSP, but all board-specific clock, memory, pin, and
peripheral settings belong to CoolingCtrlBoard.

## Target

- MCU: STM32F302CCT6 (`STM32F302xC`)
- CPU: Cortex-M4F, hard-float ABI
- Flash: 256 KiB
- SRAM: 40 KiB
- HSE: 8 MHz
- SYSCLK: 72 MHz
- APB1: 36 MHz
- APB2: 72 MHz

## Board interfaces

| Function | Pins | Configuration |
| --- | --- | --- |
| RT-Thread console | USART1 PB6/PB7 | 115200, 8-N-1, interrupt RX |
| ADC1 inputs | PA0/PA1/PA2 | Channels 1/2/3, polling, 181.5 cycles |
| CAN | PA11/PA12 | 500 kbit/s |
| Heater power | PB0 | Push-pull output, forced low at boot |
| Limit switch | PB15 | Input, no internal pull |
| Debug | PA13/PA14 | SWD |

## Safety

PB0 controls `HTR_PWR`. The BSP drives PB0 low before changing it to output
mode and the milestone application never drives it high.

## RT-Thread Studio

Import this directory with `General -> Existing Projects into Workspace`, then
build the `CoolingCtrlBoard` project. All RT-Thread, CMSIS, and STM32F3 HAL
sources required by the build are already included. See
`README_RTTHREAD_STUDIO.md` in this directory for the full Chinese guide.

## Command-line build

The project can also be built with RT-Thread Env and the Studio bundled GNU
Arm toolchain:

```text
scons -j4
```

Hardware behavior must be verified on the custom board before the heater or
CAN network is used in a live system.

## CAN OTA

The project now contains two build targets in one source tree: a 32 KiB
Bootloader at `0x08000000` and an App at `0x08008000`. Run
`tools/build_firmware.ps1 -Target All` to generate the two initial-download
BIN files and an optional combined factory image. RBL packaging, the exact
Flash map, CAN protocol, power-loss behavior, and board test procedure are
documented in `OTA移植说明.md`.
