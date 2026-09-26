@echo off
setlocal
rem Regenerates SourceArt (textures and models) and imports it into Content/Art,
rem rebuilding the materials. Close the editor first.
if "%UE_ROOT%"=="" (
  for /d %%D in ("C:\Program Files\Epic Games\UE_5.*") do set "UE_ROOT=%%D"
)
set "EDITOR_CMD=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if not exist "%EDITOR_CMD%" (
  echo Could not find UnrealEditor-Cmd.exe. Set UE_ROOT to your Unreal Engine folder.
  exit /b 1
)
set "ROOT=%~dp0.."
python "%ROOT%\Tools\generate_textures.py" || exit /b 1
python "%ROOT%\Tools\generate_models.py" || exit /b 1
"%EDITOR_CMD%" "%ROOT%\Arena.uproject" -run=pythonscript -script="%ROOT%\Tools\import_art.py" -unattended -nosplash -nullrhi
