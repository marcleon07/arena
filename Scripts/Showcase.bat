@echo off
setlocal
rem Renders screenshots offscreen (no window) into Saved\Showcase, then quits.
rem   Showcase.bat poses              characters running, strafing, crouching, jumping, aiming
rem   Showcase.bat view               first-person view of every weapon
rem   Showcase.bat items              pickup close-ups
rem   Showcase.bat ragdoll            a character killed mid-run
rem   Showcase.bat map Skyline        overview and player's-eye shots of a map
rem   Showcase.bat bots Canyon        chase camera on bots in a live match
rem The first run after changing materials waits for shaders to compile.
if "%UE_ROOT%"=="" (
  for /d %%D in ("C:\Program Files\Epic Games\UE_5.*") do set "UE_ROOT=%%D"
)
set "EDITOR=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%EDITOR%" (
  echo Could not find UnrealEditor.exe. Set UE_ROOT to your Unreal Engine folder.
  exit /b 1
)
set "PROJECT=%~dp0..\Arena.uproject"
set "SCENE=%~1"
if "%SCENE%"=="" set "SCENE=poses"
set "MAP=%~2"
if "%MAP%"=="" set "MAP=Courtyard"
set "BOTS=0"
if /i "%SCENE%"=="bots" set "BOTS=6"
"%EDITOR%" "%PROJECT%" /Engine/Maps/Entry?listen?Arena=%MAP%?Bots=%BOTS%?BotSkill=4 -game -RenderOffscreen -nosound -nosteam -ResX=1600 -ResY=900 -ArenaShowcase=%SCENE% -log=Showcase.log
echo Screenshots are in %~dp0..\Saved\Showcase
