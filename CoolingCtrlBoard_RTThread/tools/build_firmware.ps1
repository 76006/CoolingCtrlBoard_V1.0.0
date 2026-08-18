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
$outputPath = Join-Path $projectRoot 'output'
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

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
    $appSource = Join-Path $projectRoot 'rtthread.bin'
    $appOutput = Join-Path $outputPath 'cooling_app.bin'
    if ((Get-Item -LiteralPath $appSource).Length -gt (0x1A800 - 96)) {
        throw 'App BIN plus the 96-byte RBL header exceeds the download partition.'
    }
    Copy-Item -LiteralPath $appSource -Destination $appOutput -Force
}

if (($Target -eq 'Bootloader') -or ($Target -eq 'All')) {
    $bootRoot = Join-Path $projectRoot 'bootloader'
    Invoke-SconsBuild $bootRoot
    $bootSource = Join-Path $bootRoot 'cooling_bootloader.bin'
    $bootOutput = Join-Path $outputPath 'cooling_bootloader.bin'
    if ((Get-Item -LiteralPath $bootSource).Length -gt 0x8000) {
        throw 'Bootloader BIN exceeds the 32 KiB bl partition.'
    }
    if ((Get-Item -LiteralPath $bootSource).Length -lt (0x7800 + 12)) {
        throw 'Bootloader BIN does not contain the UID key slot at offset 0x7800.'
    }
    Copy-Item -LiteralPath $bootSource -Destination $bootOutput -Force
}

if ($Target -eq 'All') {
    $bootBytes = [IO.File]::ReadAllBytes((Join-Path $outputPath 'cooling_bootloader.bin'))
    $appBytes = [IO.File]::ReadAllBytes((Join-Path $outputPath 'cooling_app.bin'))
    $factoryBytes = New-Object byte[] (0x8000 + $appBytes.Length)
    for ($index = 0; $index -lt $factoryBytes.Length; $index++) {
        $factoryBytes[$index] = 0xFF
    }
    [Array]::Copy($bootBytes, 0, $factoryBytes, 0, $bootBytes.Length)
    [Array]::Copy($appBytes, 0, $factoryBytes, 0x8000, $appBytes.Length)
    [IO.File]::WriteAllBytes((Join-Path $outputPath 'cooling_factory.bin'), $factoryBytes)
}

Write-Host "Firmware generated in: $outputPath"
