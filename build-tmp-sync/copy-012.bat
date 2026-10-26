@echo off
robocopy "versions\0.1.2\backend" "backend" /MIR /NFL /NDL /NJH /NJS >NUL
robocopy "versions\0.1.2\frontend\src" "frontend\src" /MIR /NFL /NDL /NJH /NJS >NUL
robocopy "versions\0.1.2\fixtures" "fixtures" /MIR /NFL /NDL /NJH /NJS >NUL
copy /Y "versions\0.1.2\CMakeLists.txt" "CMakeLists.txt" >NUL
copy /Y "versions\0.1.2\ManiaReplayAnalyzer.exe" "ManiaReplayAnalyzer.exe" >NUL
copy /Y "versions\0.1.2\frontend\index.html" "frontend\index.html" >NUL
copy /Y "versions\0.1.2\frontend\package.json" "frontend\package.json" >NUL
copy /Y "versions\0.1.2\frontend\pnpm-lock.yaml" "frontend\pnpm-lock.yaml" >NUL
copy /Y "versions\0.1.2\frontend\pnpm-workspace.yaml" "frontend\pnpm-workspace.yaml" >NUL
copy /Y "versions\0.1.2\frontend\tsconfig.json" "frontend\tsconfig.json" >NUL
copy /Y "versions\0.1.2\frontend\vite.config.ts" "frontend\vite.config.ts" >NUL
echo COPY_DONE
