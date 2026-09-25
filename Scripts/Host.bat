@echo off
setlocal
rem Point UE_ROOT at your engine install, e.g. set UE_ROOT=C:\Program Files\Epic Games\UE_5.6
if "%UE_ROOT%"=="" (
  for /d %%D in ("C:\Program Files\Epic Games\UE_5.*") do set "UE_ROOT=%%D"
)
set "EDITOR=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%EDITOR%" (
  echo Could not find UnrealEditor.exe. Set UE_ROOT to your Unreal Engine folder.
  exit /b 1
)
set "PROJECT=%~dp0..\Arena.uproject"
start "" "%EDITOR%" "%PROJECT%" /Engine/Maps/Entry?listen -game -log -windowed -ResX=1280 -ResY=720 %*
