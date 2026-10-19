@echo off
rem Build backend only (skip frontend vite build) - for fast judgement iteration.
setlocal enabledelayedexpansion
for %%i in ("%~dp0..") do set "ROOT=%~dp0.."
for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

cmd /c ""%VCVARS%" >nul 2>&1 && "%CMAKE%" --build "!ROOT!\build\cmake" --target ManiaReplayAnalyzer" || goto :fail

taskkill /IM ManiaReplayAnalyzer.exe /F >nul 2>&1
copy /y "!ROOT!\build\cmake\bin\ManiaReplayAnalyzer.exe" "!ROOT!\release\ManiaReplayAnalyzer.exe" >nul || goto :fail
copy /y "!ROOT!\build\cmake\bin\ManiaReplayAnalyzer.exe" "!ROOT!\ManiaReplayAnalyzer.exe" >nul || goto :fail
icacls "!ROOT!\release\ManiaReplayAnalyzer.exe" /setintegritylevel M >nul || goto :fail
icacls "!ROOT!\ManiaReplayAnalyzer.exe" /setintegritylevel M >nul || goto :fail
echo BACKEND BUILD OK
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
