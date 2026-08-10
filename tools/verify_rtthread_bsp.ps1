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

Write-Output 'BSP static checks passed.'
