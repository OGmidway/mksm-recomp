@echo off
setlocal
if not defined PS2_CONTROLLER_CONFIG set "PS2_CONTROLLER_CONFIG=%~dp0config\controllers.ini"
rem Playtesting is audible by default. Set PS2_AUDIO_MUTE=1 before launching for a silent diagnostic run.
if not defined PS2_AUDIO_MUTE set "PS2_AUDIO_MUTE=0"
cd /d "%~dp0runtime"
if not exist "..\..\..\out\mk-runtime\ps2xRuntime\Release\ps2EntryRunner.exe" (
  echo The diagnostic runner has not been built. Run tools\build.ps1 first.
  pause
  exit /b 1
)
if "%PS2_AUDIO_MUTE%"=="1" (echo Audio is muted for this run.) else (echo Audio is enabled for this playtest.)
echo Shaolin Monks recompilation - experimental diagnostic build.
echo This build is not yet verified playable. Close the game window to exit.
"..\..\..\out\mk-runtime\ps2xRuntime\Release\ps2EntryRunner.exe" "..\..\..\MortalKombatShaolinMonks\SLUS_210.87"
echo Runner exit code: %errorlevel%
pause
