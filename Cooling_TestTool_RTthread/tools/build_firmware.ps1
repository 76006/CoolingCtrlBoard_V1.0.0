param(
    [ValidateSet('App', 'Bootloader', 'All')]
    [string]$Target = 'All',
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $projectRoot)
$studioRoot = Split-Path -Parent $workspaceRoot

$sconsPath = Join-Path $studioRoot 'platform\env_released\env\tools\Python27\Scripts\scons.bat'
$toolchainPath = if ($env:RTT_EXEC_PATH) {
    $env:RTT_EXEC_PATH
} else {
    Join-Path $studioRoot 'repo\Extract\ToolChain_Support_Packages\ARM\GNU_Tools_for_ARM_Embedded_Processors\5.4.1\bin'
}

if (-not (Test-Path -LiteralPath $sconsPath)) {
    throw "RT-Thread Studio SCons was not found: $sconsPath"
}
if (-not (Test-Path -LiteralPath $toolchainPath)) {
    throw "GNU Arm toolchain was not found: $toolchainPath"
}

$env:RTT_EXEC_PATH = $toolchainPath
$env:PYTHONPATH = Join-Path $projectRoot 'rt-thread\tools'

function Invoke-SconsBuild([string]$workingDirectory) {
    Push-Location $workingDirectory
    try {
        & $sconsPath "-j$Jobs"
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: $workingDirectory"
        }
    }
    finally {
        Pop-Location
    }
}

if (($Target -eq 'App') -or ($Target -eq 'All')) {
    Invoke-SconsBuild $projectRoot
}

if (($Target -eq 'Bootloader') -or ($Target -eq 'All')) {
    $bootRoot = Join-Path $projectRoot 'bootloader'
    Invoke-SconsBuild $bootRoot
}

& (Join-Path $PSScriptRoot 'export_firmware.ps1') -Target $Target
