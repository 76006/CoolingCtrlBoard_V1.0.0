param(
    [ValidateSet('App', 'Bootloader', 'All')]
    [string]$Target = 'App'
)

# Export existing artifacts only. This script never invokes a compiler.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$appElf = Join-Path $projectRoot 'Debug\rtthread.elf'
$appBin = Join-Path $projectRoot 'Debug\rtthread.bin'
$bootBin = Join-Path $projectRoot 'bootloader\cooling_bootloader.bin'
$outputPath = Join-Path $projectRoot 'output'
$appBase = 0x08008000
$appMaxSize = 0x1A800 - 96

function Assert-AppMatchesElf {
    $elfBytes = [IO.File]::ReadAllBytes($appElf)
    $binBytes = [IO.File]::ReadAllBytes($appBin)
    if (($elfBytes.Length -lt 52) -or
        ([BitConverter]::ToUInt32($elfBytes, 0) -ne 0x464C457F) -or
        ($elfBytes[4] -ne 1) -or ($elfBytes[5] -ne 1) -or
        ([BitConverter]::ToUInt16($elfBytes, 18) -ne 40)) {
        throw 'Debug/rtthread.elf must be a little-endian ARM ELF32 executable.'
    }
    if (($binBytes.Length -eq 0) -or ($binBytes.Length -gt $appMaxSize)) {
        throw 'App BIN plus the 96-byte RBL header exceeds the download partition.'
    }

    $tableOffset = [BitConverter]::ToUInt32($elfBytes, 28)
    $entrySize = [BitConverter]::ToUInt16($elfBytes, 42)
    $entryCount = [BitConverter]::ToUInt16($elfBytes, 44)
    if (($entrySize -lt 32) -or
        ($tableOffset + ([long]$entrySize * $entryCount) -gt $elfBytes.Length)) {
        throw 'Invalid ELF program header table.'
    }
    $flashEnd = 0L
    $hasAppStart = $false
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        for ($index = 0; $index -lt $entryCount; $index++) {
            $header = [int]($tableOffset + $index * $entrySize)
            if ([BitConverter]::ToUInt32($elfBytes, $header) -ne 1) { continue }
            $fileOffset = [BitConverter]::ToUInt32($elfBytes, $header + 4)
            $address = [BitConverter]::ToUInt32($elfBytes, $header + 12)
            $length = [BitConverter]::ToUInt32($elfBytes, $header + 16)
            if (($length -eq 0) -or ($address -ge 0x20000000)) { continue }
            if (($address -lt $appBase) -or
                ([long]$address + $length -gt $appBase + $appMaxSize) -or
                ([long]$fileOffset + $length -gt $elfBytes.Length)) {
                throw 'App ELF load address or size is outside the App partition.'
            }
            $binOffset = [int]($address - $appBase)
            if ([long]$binOffset + $length -gt $binBytes.Length) {
                throw 'Debug/rtthread.bin is shorter than the App ELF load image.'
            }
            $elfHash = [BitConverter]::ToString($sha.ComputeHash($elfBytes, [int]$fileOffset, [int]$length))
            $binHash = [BitConverter]::ToString($sha.ComputeHash($binBytes, $binOffset, [int]$length))
            if ($elfHash -ne $binHash) {
                throw 'Debug ELF and BIN differ. Refusing to export mismatched firmware.'
            }
            if ($address -eq $appBase) { $hasAppStart = $true }
            $flashEnd = [Math]::Max($flashEnd, [long]$binOffset + $length)
        }
    }
    finally {
        $sha.Dispose()
    }
    if ((-not $hasAppStart) -or ($flashEnd -ne $binBytes.Length)) {
        throw 'App BIN size/base does not match the App ELF at 0x08008000.'
    }
}

$exportApp = $Target -ne 'Bootloader'
# Refresh the factory image against the existing App when rebuilding Bootloader.
if (($Target -eq 'Bootloader') -and (Test-Path -LiteralPath $appElf) -and
    (Test-Path -LiteralPath $appBin)) {
    $exportApp = $true
}
$exportBoot = ($Target -ne 'App') -or (Test-Path -LiteralPath $bootBin)

# Validate all inputs before replacing any exported files.
if ($exportApp) { Assert-AppMatchesElf }
if ($exportBoot) {
    $bootSize = (Get-Item -LiteralPath $bootBin).Length
    if (($bootSize -gt 0x8000) -or ($bootSize -lt (0x7800 + 12))) {
        throw 'Bootloader BIN size or UID key-slot offset is invalid.'
    }
}

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
if ($exportApp) {
    Copy-Item -LiteralPath $appElf -Destination (Join-Path $outputPath 'cooling_app.elf') -Force
    Copy-Item -LiteralPath $appBin -Destination (Join-Path $outputPath 'cooling_app.bin') -Force
}
if ($exportBoot) {
    Copy-Item -LiteralPath $bootBin -Destination (Join-Path $outputPath 'cooling_bootloader.bin') -Force
}
if ($exportApp -and $exportBoot) {
    $bootBytes = [IO.File]::ReadAllBytes($bootBin)
    $appBytes = [IO.File]::ReadAllBytes($appBin)
    $factoryBytes = New-Object byte[] (0x8000 + $appBytes.Length)
    for ($index = 0; $index -lt $factoryBytes.Length; $index++) {
        $factoryBytes[$index] = 0xFF
    }
    [Array]::Copy($bootBytes, 0, $factoryBytes, 0, $bootBytes.Length)
    [Array]::Copy($appBytes, 0, $factoryBytes, 0x8000, $appBytes.Length)
    [IO.File]::WriteAllBytes((Join-Path $outputPath 'cooling_factory.bin'), $factoryBytes)
}
Write-Host "Firmware exported from Debug to: $outputPath"
