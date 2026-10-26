@echo off
rem Start the osu-mania-live-reader for the Blue Zenith truth capture (--once).
rem Keep this window open, then play the anchored replay in osu!.
setlocal
set "READER=C:\Users\19677\Desktop\workspace\osu-mania-live-reader"
set "LOGFILE=output\reader_bz.log"

if /i "%~1"=="--dry" goto :dry

cd /d "%READER%"
if errorlevel 1 goto :fail
if not exist "src\index.mjs" goto :fail

echo ============================================================
echo  Reader starting. KEEP THIS WINDOW OPEN.
echo  Now play the anchored replay in osu!:
echo    Blue Zenith - JINDIMENSIONS 0.9x (180bpm) OD5
echo    local score of 2026-10-03 21:43, or drag the .osr onto osu!
echo  The window closes by itself after the run.
echo ============================================================
node src\index.mjs --once --log %LOGFILE%
echo.
echo Reader exited. Output should be in:
echo   %READER%\output\  (file name contains _6ef7c488)
echo Next: copy that JSON into mania-replay-analyzer\test\sample\new\ and run
echo   node scripts\bz_capture_postcheck.mjs "test\sample\new\<file>"
pause
exit /b 0

:dry
echo READER=%READER%
echo LOGFILE=%LOGFILE%
if exist "%READER%\src\index.mjs" (echo entry=OK) else (echo entry=MISSING)
if exist "%READER%\output" (echo outdir=OK) else (echo outdir=MISSING)
exit /b 0

:fail
echo FAILED: cannot find %READER%\src\index.mjs
pause
exit /b 1
