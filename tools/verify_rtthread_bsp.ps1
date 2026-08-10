$ErrorActionPreference = 'Stop'

$bspRoot = Join-Path $PSScriptRoot '..\RT-Thread-v5.2.2\bsp\stm32\cooling-ctrl-board'

function Assert-Contains([string]$Path, [string]$Pattern)
{
    if (-not (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet))
    {
        throw "Expected '$Pattern' in $Path"
    }
}

Assert-Contains (Join-Path $bspRoot 'Kconfig') 'CoolingCtrlBoard'
Assert-Contains (Join-Path $bspRoot 'Kconfig') 'SOC_STM32F302CC'
Assert-Contains (Join-Path $bspRoot 'rtconfig.py') "MCU='STM32F302xC'"
Assert-Contains (Join-Path $bspRoot 'README.md') 'STM32F302CCT6'
Assert-Contains (Join-Path $bspRoot 'board\SConscript') 'startup_stm32f302xc\.s'
Assert-Contains (Join-Path $bspRoot 'board\linker_scripts\link.lds') 'LENGTH = 256K'
Assert-Contains (Join-Path $bspRoot 'board\linker_scripts\link.lds') 'LENGTH =  *40K'

Write-Output 'BSP static checks passed.'
