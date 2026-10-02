@echo off
rem Test script: unit tests (ctest) for backend modules.
setlocal

set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

cmd /c ""%VCVARS%" >nul 2>&1 && %CMAKE% --build "%ROOT%\build\cmake" --config Release --target mra_tests" || goto :fail
cmd /c ""%VCVARS%" >nul 2>&1 && %CMAKE% --build "%ROOT%\build\cmake" --config Release --target run_tests" || goto :fail
exit /b 0

:fail
echo TESTS BUILD/RUN FAILED
exit /b 1
