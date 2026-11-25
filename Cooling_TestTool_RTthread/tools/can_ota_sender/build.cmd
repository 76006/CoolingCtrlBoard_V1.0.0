@echo off
setlocal

set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
set "SOURCE=%~dp0CoolingCanOta.cs"
set "OUTPUT=%~dp0CoolingCanOta.exe"

if not exist "%CSC%" (
    echo Cannot find 32-bit C# compiler: %CSC%
    exit /b 1
)

"%CSC%" /nologo /optimize+ /platform:x86 /out:"%OUTPUT%" "%SOURCE%"
if errorlevel 1 exit /b 1

echo Generated: %OUTPUT%
endlocal
