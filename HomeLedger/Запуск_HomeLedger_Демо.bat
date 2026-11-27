@echo off
chcp 65001 > nul
cd /d "%~dp0"
if not exist "build\HomeLedger.exe" (
    echo Исполняемый файл не найден. Запустите сборку CMake.
    pause
    exit /b 1
)
"build\HomeLedger.exe" --data demo_data --demo %*
pause
