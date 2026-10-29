@echo off
rem Build the single executable: frontend dist -> embedded resources -> exe.
rem 0.1.3+: source root = versions/<ver>, deploy target = versions/<ver>/ManiaReplayAnalyzer.exe.
setlocal enabledelayedexpansion
set "VER=0.1.3"

rem Canonical workspace root (resolves %~dp0.. to an absolute path).
for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "PNPM=C:\Users\19677\AppData\Roaming\npm\pnpm.cmd"

echo [1/3] Building frontend...
if exist "!ROOT!\versions\!VER!\frontend\package.json" (
    if not exist "!ROOT!\versions\!VER!\frontend\node_modules" (
        call "%PNPM%" -C "!ROOT!\versions\!VER!\frontend" install --ignore-scripts || goto :fail
    )
    call "%PNPM%" -C "!ROOT!\versions\!VER!\frontend" run build || goto :fail
) else (
    echo WARNING: no frontend/package.json; building shell-only
)

echo [2/3] Configuring CMake (Ninja, Release)...
cmd /c ""%VCVARS%" >nul 2>&1 && "%CMAKE%" -S "!ROOT!\versions\!VER!" -B "!ROOT!\build\cmake-!VER!" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA%"" || goto :fail

echo [3/3] Building ManiaReplayAnalyzer...
cmd /c ""%VCVARS%" >nul 2>&1 && "%CMAKE%" --build "!ROOT!\build\cmake-!VER!" --target ManiaReplayAnalyzer" || goto :fail

echo [4/4] Deploying (kill old instance -> copy to project root -> fix integrity label)...
taskkill /IM ManiaReplayAnalyzer.exe /F >nul 2>&1
copy /y "!ROOT!\build\cmake-!VER!\bin\ManiaReplayAnalyzer.exe" "!ROOT!\versions\!VER!\ManiaReplayAnalyzer.exe" >nul || goto :fail
rem 项目树文件夹带有 Low Mandatory Integrity 标签(沙箱实验遗留),exe 会继承并被 SmartScreen 拦截;
rem 给 exe 设置显式 Medium 标签覆盖继承即可消除弹窗(已验证)。
icacls "!ROOT!\versions\!VER!\ManiaReplayAnalyzer.exe" /setintegritylevel M >nul || goto :fail
echo.
echo Output: !ROOT!\versions\!VER!\ManiaReplayAnalyzer.exe
exit /b 0

:fail
echo BUILD FAILED
exit /b 1