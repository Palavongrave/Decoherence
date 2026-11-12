@echo off
rem Copies the built Decoherence.vst3 into C:\Program Files\Common Files\VST3.
rem That is the standard VST3 folder, and the only folder FL Studio scans for VST3 plugins
rem by default (folders like D:\VSTI are scanned for VST2 .dll files only).
rem Writing there needs admin rights, so this asks for elevation once.
setlocal

set "SRC=%~dp0build\Decoherence_artefacts\Release\VST3\Decoherence.vst3"
set "DST=%CommonProgramFiles%\VST3\Decoherence.vst3"

if not exist "%SRC%" goto notbuilt

net session >nul 2>&1
if errorlevel 1 goto elevate

robocopy "%SRC%" "%DST%" /MIR /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto copyfailed
echo Installed: %DST%
exit /b 0

:notbuilt
echo Nothing to install yet - run build.bat first.
exit /b 1

:copyfailed
echo Copy failed. Is a DAW using the plugin right now? Close it and try again.
exit /b 1

:elevate
echo Asking Windows for permission to write to %CommonProgramFiles%\VST3 ...
powershell -NoProfile -Command "$p = Start-Process -FilePath '%~f0' -Verb RunAs -Wait -PassThru; exit $p.ExitCode"
exit /b %ERRORLEVEL%
