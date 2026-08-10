$ErrorActionPreference = 'Stop'

$bspRoot = Join-Path $PSScriptRoot '..\RT-Thread-v5.2.2\bsp\stm32\cooling-ctrl-board'

function Assert-Contains([string]$Path, [string]$Pattern)
{
    if (-not (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet))
    {
        throw "Expected '$Pattern' in $Path"
    }
}

function Assert-NotContains([string]$Path, [string]$Pattern)
{
    if (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)
    {
        throw "Unexpected '$Pattern' in $Path"
    }
}

Assert-Contains (Join-Path $bspRoot 'Kconfig') 'CoolingCtrlBoard'
Assert-Contains (Join-Path $bspRoot 'Kconfig') 'SOC_STM32F302CC'
Assert-Contains (Join-Path $bspRoot 'rtconfig.py') "MCU='STM32F302xC'"
Assert-Contains (Join-Path $bspRoot 'README.md') 'STM32F302CCT6'
Assert-Contains (Join-Path $bspRoot 'board\SConscript') 'startup_stm32f302xc\.s'
Assert-Contains (Join-Path $bspRoot 'board\linker_scripts\link.lds') 'LENGTH = 256K'
Assert-Contains (Join-Path $bspRoot 'board\linker_scripts\link.lds') 'LENGTH =  *40K'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'RCC_PLL_MUL9'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'RCC_HCLK_DIV2'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'RCC_USART1CLKSOURCE_PCLK2'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'RCC_ADC12PLLCLK_DIV1'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'HAL_GPIO_WritePin\(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET\)'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'GPIO_MODE_OUTPUT_PP'
Assert-Contains (Join-Path $bspRoot 'board\board.c') 'GPIO_PIN_15'
Assert-Contains (Join-Path $bspRoot 'board\board.h') 'BOARD_HEATER_POWER_PIN.*GET_PIN\(B, 0\)'
Assert-Contains (Join-Path $bspRoot 'board\board.h') 'BOARD_LIMIT_SWITCH_PIN.*GET_PIN\(B, 15\)'

$boardSource = Get-Content -LiteralPath (Join-Path $bspRoot 'board\board.c') -Raw
$heaterLow = $boardSource.IndexOf('HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)')
$heaterOutput = $boardSource.IndexOf('gpio.Mode = GPIO_MODE_OUTPUT_PP')
$safeInit = $boardSource.IndexOf('cooling_board_safe_gpio_init();')
$componentInit = $boardSource.IndexOf('rt_components_board_init();')
if ($heaterLow -lt 0 -or $heaterOutput -lt 0 -or $heaterLow -gt $heaterOutput)
{
    throw 'PB0 must be driven low before it is configured as an output.'
}
if ($safeInit -lt 0 -or $componentInit -lt 0 -or $safeInit -gt $componentInit)
{
    throw 'Safe GPIO initialization must run before RT-Thread components.'
}

Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_SERIAL'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_CONSOLE'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_CONSOLE_DEVICE_NAME "uart1"'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_FINSH'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'FINSH_USING_MSH'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'BSP_USING_UART1'
Assert-NotContains (Join-Path $bspRoot 'rtconfig.h') 'BSP_UART1_.*USING_DMA'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_PIN_6'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_PIN_7'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_AF7_USART1'

$adcDriver = Join-Path $bspRoot '..\libraries\HAL_Drivers\drivers\drv_adc.c'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_ADC'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'BSP_USING_ADC1'
Assert-Contains (Join-Path $bspRoot 'board\board.h') 'BSP_ADC_SAMPLING_TIME.*ADC_SAMPLETIME_181CYCLES_5'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_PIN_0.*GPIO_PIN_1.*GPIO_PIN_2'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_MODE_ANALOG'
Assert-Contains $adcDriver 'BSP_ADC_SAMPLING_TIME'
Assert-Contains $adcDriver 'HAL_ADCEx_Calibration_Start.*ADC_SINGLE_ENDED'

$canDriver = Join-Path $bspRoot '..\libraries\HAL_Drivers\drivers\drv_can.c'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'RT_USING_CAN'
Assert-Contains (Join-Path $bspRoot 'rtconfig.h') 'BSP_USING_CAN1'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_PIN_11.*GPIO_PIN_12'
Assert-Contains (Join-Path $bspRoot 'board\CubeMX_Config\Src\stm32f3xx_hal_msp.c') 'GPIO_AF9_CAN'
Assert-Contains $canDriver 'SOC_SERIES_STM32F3'
Assert-Contains $canDriver 'CAN500kBaud.*CAN_SJW_1TQ.*CAN_BS1_15TQ.*CAN_BS2_2TQ.*4'
Assert-Contains $canDriver 'AutoBusOff = ENABLE'
Assert-Contains $canDriver 'AutoRetransmission = ENABLE'

$canRate = 36000000 / 4 / (1 + 15 + 2)
if ($canRate -ne 500000)
{
    throw "CAN bit rate is $canRate, expected 500000."
}

Write-Output 'BSP static checks passed.'
