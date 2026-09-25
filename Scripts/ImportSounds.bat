@echo off
setlocal
rem Regenerates SourceAudio/*.wav and imports them into Content/Audio.
if "%UE_ROOT%"=="" (
  for /d %%D in ("C:\Program Files\Epic Games\UE_5.*") do set "UE_ROOT=%%D"
)
set "EDITOR_CMD=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if not exist "%EDITOR_CMD%" (
  echo Could not find UnrealEditor-Cmd.exe. Set UE_ROOT to your Unreal Engine folder.
  exit /b 1
)
set "ROOT=%~dp0.."
python "%ROOT%\Tools\generate_sounds.py" || exit /b 1
"%EDITOR_CMD%" "%ROOT%\Arena.uproject" -run=pythonscript -script="%ROOT%\Tools\import_sounds.py" -unattended -nosplash -nullrhi