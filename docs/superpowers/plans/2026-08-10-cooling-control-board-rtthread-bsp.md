# CoolingCtrlBoard RT-Thread BSP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and verify a GNU Arm GCC RT-Thread Studio project for CoolingCtrlBoard V1.0 using RT-Thread Standard v5.2.2 and STM32F302CCT6.

**Architecture:** Download the official v5.2.2 release into a local source staging tree, clone the official `stm32f302-st-nucleo` BSP as a same-SoC structural baseline, then replace every board-specific property with the CoolingCtrlBoard clock, memory map, pins, and peripheral configuration. RT-Thread owns startup, exceptions, HAL handles, and device drivers; CubeMX remains the hardware configuration reference and contributes only board clock and MSP details.

**Tech Stack:** RT-Thread Standard v5.2.2, STM32F3 HAL, SCons/Kconfig, GNU Arm Embedded GCC, RT-Thread Studio, STM32CubeMX reference project, ST-LINK.

## Global Constraints

- Use official RT-Thread release tag `v5.2.2`; never substitute `master` or an older release without user approval.
- Target `STM32F302xC`, Cortex-M4F, FPv4-SP-D16, hard-float ABI.
- Use exactly 256 KiB Flash at `0x08000000` and 40 KiB SRAM at `0x20000000`.
- Preserve `CoolingCtrlBoard_CubeMX/` and never generate RT-Thread files into it.
- Do not compile CubeMX `main.c`, `stm32f3xx_it.c`, `syscalls.c`, or `sysmem.c` into RT-Thread.
- Use only RT-Thread's bundled STM32F3 HAL in the RT-Thread build; do not copy `CoolingCtrlBoard_CubeMX/Drivers`.
- PB0 (`HTR_PWR`) must be driven low before output-mode initialization and before the scheduler starts.
- USART1 is `PB6/PB7`, `115200 8-N-1`, interrupt RX, no DMA.
- ADC1 uses PA0/PA1/PA2, software-triggered single conversions, no DMA, and `ADC_SAMPLETIME_181CYCLES_5`.
- CAN uses PA11/PA12, APB1 36 MHz, 500 kbit/s, Prescaler 4, BS1 15 TQ, BS2 2 TQ, SJW 1 TQ.
- The first milestone must never expose a command or automatic path that drives PB0 high.
- Hardware flashing and live electrical validation are outside this plan.

---

### Task 1: Acquire and Pin the Official RT-Thread Source

**Files:**
- Create: `.gitignore`
- Create from official archive: `RT-Thread-v5.2.2/`
- Create temporary download: `tmp/rt-thread-v5.2.2.zip`

**Interfaces:**
- Consumes: Official GitHub release archive for tag `v5.2.2`.
- Produces: A version-pinned RT-Thread source tree used by every later task.

- [ ] **Step 1: Add dependency and generated-output ignore rules**

Create `.gitignore` with these exact rules. The negation chain keeps only the custom BSP trackable while ignoring the downloaded upstream tree and generated Studio distribution:

```gitignore
/RT-Thread-v5.2.2/*
!/RT-Thread-v5.2.2/bsp/
/RT-Thread-v5.2.2/bsp/*
!/RT-Thread-v5.2.2/bsp/stm32/
/RT-Thread-v5.2.2/bsp/stm32/*
!/RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/
!/RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/**
/CoolingCtrlBoard_RTThread/
/tmp/rt-thread-v5.2.2.zip
```

- [ ] **Step 2: Verify the destination is safe**

Run:

```powershell
Test-Path -LiteralPath 'RT-Thread-v5.2.2'
Test-Path -LiteralPath 'CoolingCtrlBoard_RTThread'
```

Expected: both return `False`. If either is `True`, inspect it and stop rather than deleting or overwriting it.

- [ ] **Step 3: Download only the v5.2.2 release archive**

Run with approved network access:

```powershell
Invoke-WebRequest -UseBasicParsing `
  'https://github.com/RT-Thread/rt-thread/archive/refs/tags/v5.2.2.zip' `
  -OutFile 'tmp\rt-thread-v5.2.2.zip'
```

Expected: HTTP download succeeds and the ZIP is non-empty.

- [ ] **Step 4: Extract and normalize the source directory name**

Run:

```powershell
Expand-Archive -LiteralPath 'tmp\rt-thread-v5.2.2.zip' -DestinationPath 'tmp\rt-thread-v5.2.2-extract'
Move-Item -LiteralPath 'tmp\rt-thread-v5.2.2-extract\rt-thread-5.2.2' -Destination 'RT-Thread-v5.2.2'
```

Expected: `RT-Thread-v5.2.2/include/rtthread.h` and `RT-Thread-v5.2.2/bsp/stm32/stm32f302-st-nucleo` exist.

- [ ] **Step 5: Verify version macros and the reference BSP**

Run:

```powershell
Select-String -Path 'RT-Thread-v5.2.2\include\rtdef.h' -Pattern 'RT_VERSION|RT_SUBVERSION|RT_REVISION'
Get-ChildItem -LiteralPath 'RT-Thread-v5.2.2\bsp\stm32\stm32f302-st-nucleo'
```

Expected: version components resolve to 5.2.2 and the F302 reference BSP is present.

- [ ] **Step 6: Commit dependency policy**

```powershell
git add -- .gitignore
git commit -m "build: pin RT-Thread v5.2.2 dependency layout"
```

---

### Task 2: Create the CoolingCtrlBoard BSP Skeleton

**Files:**
- Create by copying: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/Kconfig`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/SConstruct`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/rtconfig.py`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/README.md`
- Modify: the BSP linker script selected by `rtconfig.py`
- Replace or add: the GCC `startup_stm32f302xc.s` selected by the BSP build

**Interfaces:**
- Consumes: Official `stm32f302-st-nucleo` BSP structure and CubeMX xC startup/linker facts.
- Produces: A board-named BSP that selects STM32F302xC and the correct memory/FPU model.

- [ ] **Step 1: Copy the same-family BSP as a structural baseline**

Run:

```powershell
Copy-Item -LiteralPath 'RT-Thread-v5.2.2\bsp\stm32\stm32f302-st-nucleo' `
  -Destination 'RT-Thread-v5.2.2\bsp\stm32\cooling-ctrl-board' -Recurse
```

Expected: the new BSP has its own `Kconfig`, `SConstruct`, `rtconfig.py`, `board`, and `applications` content.

- [ ] **Step 2: Write a failing static identity check**

Create `tools/verify_rtthread_bsp.ps1` with initial checks that intentionally fail before modification:

```powershell
$ErrorActionPreference = 'Stop'
$bspRoot = Join-Path $PSScriptRoot '..\RT-Thread-v5.2.2\bsp\stm32\cooling-ctrl-board'

function Assert-Contains([string]$Path, [string]$Pattern) {
    if (-not (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)) {
        throw "Expected '$Pattern' in $Path"
    }
}

Assert-Contains (Join-Path $bspRoot 'Kconfig') 'CoolingCtrlBoard'
Assert-Contains (Join-Path $bspRoot 'rtconfig.py') 'STM32F302xC'
Assert-Contains (Join-Path $bspRoot 'README.md') 'STM32F302CCT6'
Write-Output 'BSP static checks passed.'
```

- [ ] **Step 3: Run the identity check and confirm failure**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File 'tools\verify_rtthread_bsp.ps1'
```

Expected: FAIL because the copied BSP still identifies the Nucleo board and x8 device.

- [ ] **Step 4: Replace board identity and SoC selection**

Apply these semantic changes:

```text
Board menu/name: CoolingCtrlBoard V1.0
SoC model: STM32F302CCT6
CMSIS macro: STM32F302xC
Console device: uart1
HSE value: 8000000
System clock: 72000000
```

In `rtconfig.py`, retain and verify these GCC flags:

```python
DEVICE = ' -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard'
CFLAGS = DEVICE + ' -Wall'
AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp'
LFLAGS = DEVICE + ' -Wl,--gc-sections,-Map=rtthread.map,-cref,-u,Reset_Handler'
```

- [ ] **Step 5: Replace x8 startup and memory limits**

Use `CoolingCtrlBoard_CubeMX/startup_stm32f302xc.s` as the xC vector-table source, preserving the RT-Thread build's assembler flags. Modify the BSP's active linker script so its MEMORY block is exactly:

```ld
MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 40K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
}
```

Retain all RT-Thread linker sections from the reference BSP, including the initialization function table. Do not replace the full RT-Thread linker script with CubeMX's standalone script.

- [ ] **Step 6: Re-run the identity check**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File 'tools\verify_rtthread_bsp.ps1'
```

Expected: PASS for board identity, device macro, and README checks.

- [ ] **Step 7: Commit the skeleton**

```powershell
git add -- tools/verify_rtthread_bsp.ps1 RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board
git commit -m "bsp: add CoolingCtrlBoard STM32F302xC skeleton"
```

---

### Task 3: Implement Clock and Safe GPIO Bring-up

**Files:**
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/board/board.c`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/board/board.h`
- Modify or create: the BSP-local `stm32f3xx_hal_msp.c` under the board's CubeMX configuration directory
- Modify: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: 8 MHz HSE, 72/36/72 MHz clock tree, PB0 heater output, PB15 switch input.
- Produces: `SystemClock_Config`/board clock initialization and a safe pre-scheduler GPIO state.

- [ ] **Step 1: Extend the static test for safety and clock constants**

Add checks for these exact source tokens:

```powershell
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'RCC_PLL_MUL9'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'RCC_HCLK_DIV2'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'HAL_GPIO_WritePin\(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET\)'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'GPIO_MODE_OUTPUT_PP'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'GPIO_PIN_15'
```

- [ ] **Step 2: Run the check and confirm failure**

Expected: FAIL because the copied BSP still has Nucleo clock/GPIO behavior.

- [ ] **Step 3: Adapt the CubeMX clock configuration**

Implement the clock function with these exact fields:

```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
RCC_OscInitStruct.HSEState = RCC_HSE_ON;
RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
RCC_OscInitStruct.HSIState = RCC_HSI_ON;
RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
```

Also set USART1 to PCLK2 and ADC12 to PLL clock division 1, matching the approved CubeMX configuration.

- [ ] **Step 4: Implement early GPIO safety**

Before configuring PB0 as output, execute:

```c
__HAL_RCC_GPIOB_CLK_ENABLE();
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

GPIO_InitTypeDef gpio = {0};
gpio.Pin = GPIO_PIN_0;
gpio.Mode = GPIO_MODE_OUTPUT_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOB, &gpio);

gpio.Pin = GPIO_PIN_15;
gpio.Mode = GPIO_MODE_INPUT;
gpio.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOB, &gpio);
```

Call this board-safe GPIO function from `rt_hw_board_init()` before component initialization and before the scheduler starts.

- [ ] **Step 5: Define board pin aliases**

Expose stable names in `board.h`:

```c
#define BOARD_HEATER_POWER_PIN   GET_PIN(B, 0)
#define BOARD_LIMIT_SWITCH_PIN   GET_PIN(B, 15)
```

- [ ] **Step 6: Run static checks**

Expected: PASS for clock fields and PB0/PB15 initialization ordering.

- [ ] **Step 7: Commit board bring-up**

```powershell
git add -- RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board tools/verify_rtthread_bsp.ps1
git commit -m "bsp: configure CoolingCtrlBoard clock and safe GPIO"
```

---

### Task 4: Enable RT-Thread Core Configuration and USART1 Console

**Files:**
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/Kconfig`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/.config`
- Regenerate: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/rtconfig.h`
- Modify: BSP-local `stm32f3xx_hal_conf.h`
- Modify: BSP-local `stm32f3xx_hal_msp.c`
- Modify: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: PB6/PB7 USART1 wiring and RT-Thread STM32 serial driver.
- Produces: registered `uart1`, interrupt RX, and MSH console at 115200 bit/s.

- [ ] **Step 1: Add failing console configuration checks**

Check generated configuration for:

```powershell
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_SERIAL'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_CONSOLE'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_CONSOLE_DEVICE_NAME.*uart1'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_FINSH'
Assert-Contains (Join-Path $bspRoot 'board\board.h') 'BSP_USING_UART1'
```

- [ ] **Step 2: Configure the minimal RT-Thread feature set**

Enable the Kconfig selections that generate these macros:

```c
#define RT_USING_HEAP
#define RT_USING_DEVICE
#define RT_USING_PIN
#define RT_USING_SERIAL
#define RT_USING_CONSOLE
#define RT_CONSOLE_DEVICE_NAME "uart1"
#define RT_USING_FINSH
#define FINSH_USING_MSH
#define BSP_USING_GPIO
#define BSP_USING_UART
#define BSP_USING_UART1
```

Keep UART DMA disabled.

- [ ] **Step 3: Enable the required HAL modules**

The BSP-local HAL config must define at least:

```c
#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
```

- [ ] **Step 4: Implement USART1 MSP pin and IRQ setup**

Use PB6 TX and PB7 RX with AF7. Keep TX no-pull and RX pull-up. Enable USART1 clock and IRQ. Do not add `USART1_IRQHandler`; the RT-Thread STM32 serial driver owns it.

```c
GPIO_InitStruct.Pin = GPIO_PIN_6;
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

GPIO_InitStruct.Pin = GPIO_PIN_7;
GPIO_InitStruct.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
```

- [ ] **Step 5: Regenerate `rtconfig.h` non-interactively**

Use the v5.2.2 Kconfig tooling supplied by RT-Thread/Env to refresh `rtconfig.h` from `.config`. Do not hand-edit a generated `rtconfig.h` without keeping `.config` consistent.

- [ ] **Step 6: Run static checks**

Expected: all console, UART1, and FinSH checks pass; no UART DMA macro is present.

- [ ] **Step 7: Commit USART1 console support**

```powershell
git add -- RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board tools/verify_rtthread_bsp.ps1
git commit -m "bsp: enable USART1 MSH console"
```

---

### Task 5: Register ADC1 With Board-Specific Sampling and Calibration

**Files:**
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/Kconfig`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/.config`
- Regenerate: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/rtconfig.h`
- Modify: BSP-local `stm32f3xx_hal_conf.h`
- Modify: BSP-local `stm32f3xx_hal_msp.c`
- Modify if required by v5.2.2 driver API: `RT-Thread-v5.2.2/bsp/stm32/libraries/HAL_Drivers/drv_adc.c`
- Create if common driver needs a patch: `patches/rt-thread-v5.2.2-stm32-adc-board-config.patch`
- Modify: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: ADC channels 1/2/3 and RT-Thread `rt_adc_read()` API.
- Produces: registered `adc1`, single-channel polling, 181.5-cycle sampling, one-time calibration.

- [ ] **Step 1: Add failing ADC checks**

Check for:

```powershell
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_ADC'
Assert-Contains (Join-Path $bspRoot 'board\board.h') 'BSP_USING_ADC1'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Core\Src\stm32f3xx_hal_msp.c') 'GPIO_PIN_0.*GPIO_PIN_1.*GPIO_PIN_2'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Core\Src\stm32f3xx_hal_msp.c') 'GPIO_MODE_ANALOG'
```

Also add a repository-wide check that the effective ADC driver contains `ADC_SAMPLETIME_181CYCLES_5` and `HAL_ADCEx_Calibration_Start` for STM32F3.

- [ ] **Step 2: Enable ADC framework and HAL**

Generate these macros through Kconfig/HAL config:

```c
#define RT_USING_ADC
#define BSP_USING_ADC
#define BSP_USING_ADC1
#define HAL_ADC_MODULE_ENABLED
```

- [ ] **Step 3: Configure ADC GPIO MSP**

Use a single GPIO initialization for PA0, PA1, and PA2:

```c
__HAL_RCC_ADC12_CLK_ENABLE();
__HAL_RCC_GPIOA_CLK_ENABLE();
GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
GPIO_InitStruct.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

- [ ] **Step 4: Make the common ADC driver board-configurable**

Inspect v5.2.2 `drv_adc.c` first. If it already exposes a board sampling-time macro and an F3 calibration hook, set those macros in `board.h`. Otherwise apply this minimal generic behavior:

```c
#ifndef BSP_ADC_SAMPLING_TIME
#define BSP_ADC_SAMPLING_TIME ADC_SAMPLETIME_181CYCLES_5
#endif

adc_config.SamplingTime = BSP_ADC_SAMPLING_TIME;
```

For the F3 ADC initialization path, perform calibration once after successful `HAL_ADC_Init` and before registering the device:

```c
if (HAL_ADCEx_Calibration_Start(adc_handler, ADC_SINGLE_ENDED) != HAL_OK)
{
    return -RT_ERROR;
}
```

Record any common-driver change as `patches/rt-thread-v5.2.2-stm32-adc-board-config.patch` so the ignored upstream tree can be reproduced.

- [ ] **Step 5: Regenerate configuration and run checks**

Expected: ADC1 framework macros, three analog pins, 181.5-cycle sampling, and one calibration call are all found.

- [ ] **Step 6: Commit ADC support**

```powershell
git add -- RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board patches tools/verify_rtthread_bsp.ps1
git commit -m "bsp: add calibrated ADC1 sensor inputs"
```

---

### Task 6: Register CAN at 500 kbit/s

**Files:**
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/Kconfig`
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/.config`
- Regenerate: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/rtconfig.h`
- Modify: BSP-local `stm32f3xx_hal_conf.h`
- Modify: BSP-local `stm32f3xx_hal_msp.c`
- Modify: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: PA11/PA12 CAN pins, 36 MHz APB1, RT-Thread CAN device framework.
- Produces: a registered CAN device configured for normal-mode 500 kbit/s with automatic retry and bus-off recovery.

- [ ] **Step 1: Add failing CAN checks**

Check for:

```powershell
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_CAN'
Assert-Contains (Join-Path $bspRoot 'board\board.h') 'BSP_USING_CAN1'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Core\Src\stm32f3xx_hal_msp.c') 'GPIO_AF9_CAN'
```

Add an arithmetic assertion to the verification script:

```powershell
$canRate = 36000000 / 4 / (1 + 15 + 2)
if ($canRate -ne 500000) { throw "CAN bit rate is $canRate, expected 500000" }
```

- [ ] **Step 2: Enable CAN framework and HAL**

Generate:

```c
#define RT_USING_CAN
#define BSP_USING_CAN
#define BSP_USING_CAN1
#define HAL_CAN_MODULE_ENABLED
```

Use the actual F302 driver symbol from the v5.2.2 STM32 Kconfig if it names the single instance `BSP_USING_CAN` rather than `BSP_USING_CAN1`; the generated `rtconfig.h` and driver table must register exactly one CAN device.

- [ ] **Step 3: Configure CAN MSP pins and interrupts**

Use:

```c
__HAL_RCC_CAN1_CLK_ENABLE();
__HAL_RCC_GPIOA_CLK_ENABLE();
GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
GPIO_InitStruct.Alternate = GPIO_AF9_CAN;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

Enable the CAN RX/TX/SCE interrupts required by the v5.2.2 STM32 CAN driver. Do not copy CubeMX CAN IRQ handlers.

- [ ] **Step 4: Verify and, if necessary, extend the F3 CAN timing table**

Inspect the v5.2.2 STM32 CAN driver's 500 kbit/s entry for a 36 MHz CAN clock. Accept it only if its formula evaluates to 500000 exactly. If the table lacks this F3 clock case, add the exact configuration:

```c
Prescaler = 4;
SyncJumpWidth = CAN_SJW_1TQ;
TimeSeg1 = CAN_BS1_15TQ;
TimeSeg2 = CAN_BS2_2TQ;
AutoBusOff = ENABLE;
AutoRetransmission = ENABLE;
```

Keep the normal operating mode. Record a common-driver modification as a reproducible patch under `patches/`.

- [ ] **Step 5: Regenerate configuration and run checks**

Expected: one CAN device macro, AF9 pins, and exact 500000 bit/s arithmetic pass.

- [ ] **Step 6: Commit CAN support**

```powershell
git add -- RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board patches tools/verify_rtthread_bsp.ps1
git commit -m "bsp: add 500 kbit CAN device"
```

---

### Task 7: Add a Safe Application Smoke Test

**Files:**
- Modify: `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/applications/main.c`
- Modify: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: `uart1`, `adc1`, CAN device registration, PB0/PB15 pin aliases.
- Produces: a boot-time status report that never enables the heater.

- [ ] **Step 1: Add failing smoke-test checks**

Require the application to contain:

```powershell
Assert-Contains (Join-Path $bspRoot 'applications\main.c') 'CoolingCtrlBoard'
Assert-Contains (Join-Path $bspRoot 'applications\main.c') 'rt_device_find\("uart1"\)'
Assert-Contains (Join-Path $bspRoot 'applications\main.c') 'rt_device_find\("adc1"\)'
Assert-Contains (Join-Path $bspRoot 'applications\main.c') 'BOARD_HEATER_POWER_PIN.*PIN_LOW'
```

Add a negative check that rejects `rt_pin_write(BOARD_HEATER_POWER_PIN, PIN_HIGH)`.

- [ ] **Step 2: Implement the boot report**

Implement `main()` with this behavior:

```c
int main(void)
{
    rt_pin_mode(BOARD_HEATER_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(BOARD_HEATER_POWER_PIN, PIN_LOW);

    rt_kprintf("CoolingCtrlBoard / STM32F302CCT6 / %u Hz\n",
               (unsigned int)SystemCoreClock);
    rt_kprintf("uart1: %s\n", rt_device_find("uart1") ? "ready" : "missing");
    rt_kprintf("adc1: %s\n", rt_device_find("adc1") ? "ready" : "missing");
    rt_kprintf("can: %s\n", rt_device_find("can1") ? "ready" : "missing");
    rt_kprintf("limit switch: %d\n", rt_pin_read(BOARD_LIMIT_SWITCH_PIN));
    return RT_EOK;
}
```

If the v5.2.2 STM32 CAN driver registers the single peripheral as `can` instead of `can1`, use that exact registered name consistently in the application and README.

- [ ] **Step 3: Run the static checks**

Expected: PASS, including the negative heater-high assertion.

- [ ] **Step 4: Commit the smoke test**

```powershell
git add -- RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board/applications/main.c tools/verify_rtthread_bsp.ps1
git commit -m "test: add safe board bring-up report"
```

---

### Task 8: Compile and Inspect the Firmware

**Files:**
- Generated in BSP build directory: `rtthread.elf`, `rtthread.bin`, `rtthread.map`
- Modify as errors require: only files already owned by Tasks 2-7
- Update: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: Complete custom BSP and an installed GNU Arm toolchain.
- Produces: a linkable STM32F302xC firmware image with verified memory and symbol ownership.

- [ ] **Step 1: Locate the Studio GNU Arm toolchain**

Verify:

```powershell
Get-ChildItem -LiteralPath 'D:\RT-ThreadStudio\repo\Extract\ToolChain_Support_Packages\ARM\GNU_Tools_for_ARM_Embedded_Processors' -Directory
```

Add the selected toolchain's `bin` directory only to the current build process environment. Do not modify the global Windows PATH.

- [ ] **Step 2: Confirm compiler target and FPU support**

Run:

```powershell
arm-none-eabi-gcc --version
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -dM -E -x c NUL
```

Expected: GCC runs and accepts the approved Cortex-M4F hard-float flags.

- [ ] **Step 3: Run the static verification suite**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File 'tools\verify_rtthread_bsp.ps1'
```

Expected: PASS before invoking the compiler.

- [ ] **Step 4: Build the BSP**

From `RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board`, run:

```powershell
scons -j4
```

Expected: exit code 0 and production of `rtthread.elf` plus map and binary outputs. If GCC 5.4.1 fails because the compiler is too old, install or select a compatible newer GNU Arm compiler while preserving all approved CPU/FPU flags.

- [ ] **Step 5: Verify memory usage**

Run:

```powershell
arm-none-eabi-size rtthread.elf
```

Expected: Flash sections fit below 262144 bytes and total runtime RAM sections plus configured heap/stack fit below 40960 bytes.

- [ ] **Step 6: Verify symbol ownership**

Run:

```powershell
arm-none-eabi-nm rtthread.elf | Select-String 'Reset_Handler|SysTick_Handler|PendSV_Handler|SVC_Handler|main'
```

Expected: one definition of each exception handler and one RT-Thread application `main`; no duplicate-symbol link errors.

- [ ] **Step 7: Commit build fixes and verification updates**

```powershell
git add -- RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board patches tools/verify_rtthread_bsp.ps1
git commit -m "build: verify CoolingCtrlBoard RT-Thread firmware"
```

---

### Task 9: Generate the RT-Thread Studio Import Project

**Files:**
- Generate: `CoolingCtrlBoard_RTThread/`
- Create: `README_RTTHREAD_STUDIO.md`
- Modify: `tools/verify_rtthread_bsp.ps1`

**Interfaces:**
- Consumes: Successfully compiled custom BSP.
- Produces: A standalone RT-Thread Studio import directory and exact user import instructions.

- [ ] **Step 1: Generate the Studio distribution**

From the custom BSP directory, run the v5.2.2-supported command:

```powershell
scons --dist-ide --project-path='D:\workspace\CoolingCtrlBoard_V1.0.0\CoolingCtrlBoard_RTThread' --project-name=CoolingCtrlBoard
```

Expected: the destination is created with RT-Thread Studio/Eclipse project metadata and the referenced source tree needed for a standalone import.

- [ ] **Step 2: Verify generated metadata**

Require at minimum:

```text
CoolingCtrlBoard_RTThread/.project
CoolingCtrlBoard_RTThread/.cproject
CoolingCtrlBoard_RTThread/applications/
CoolingCtrlBoard_RTThread/board/
```

Also search `.cproject` and build settings for:

```text
STM32F302xC
mcpu=cortex-m4
mfpu=fpv4-sp-d16
mfloat-abi=hard
```

- [ ] **Step 3: Write Studio import instructions**

Create `README_RTTHREAD_STUDIO.md` with:

```text
1. Open RT-Thread Studio.
2. File -> Import.
3. Select "RT-Thread Studio Projects into Workspace".
4. Browse to CoolingCtrlBoard_RTThread.
5. Import CoolingCtrlBoard.
6. Select ST-LINK and SWD.
7. Build before attempting Download or Debug.
8. Connect a 3.3 V UART adapter to P2: TX adapter -> PB7/RX, RX adapter -> PB6/TX, and common GND.
9. Use 115200 8-N-1 and confirm the MSH prompt.
```

Document that CAN hardware uses a fixed 120-ohm termination resistor and that PB0 remains deliberately unavailable for activation in the milestone firmware.

- [ ] **Step 4: Run final verification**

Extend and run `tools/verify_rtthread_bsp.ps1` to verify the Studio metadata and compiler flags. Re-run the BSP build after distribution generation to ensure generation did not alter source configuration.

Expected: static checks pass, BSP rebuild exits 0, and the Studio directory is ready to import.

- [ ] **Step 5: Commit delivery documentation**

```powershell
git add -- README_RTTHREAD_STUDIO.md tools/verify_rtthread_bsp.ps1 RT-Thread-v5.2.2/bsp/stm32/cooling-ctrl-board
git commit -m "docs: add RT-Thread Studio import guide"
```

- [ ] **Step 6: Report delivery state**

Report:

```text
Official source version and archive source
Custom BSP path
Studio import path
Compiler version
Build exit status
Flash/RAM usage
Static verification status
Known hardware-only checks still outstanding
```

Do not claim UART, ADC, CAN, or heater hardware functionality until the physical board is flashed and tested.
