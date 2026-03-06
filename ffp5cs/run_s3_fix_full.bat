@echo off
REM Build wroom-s3-test puis workflow erase + flash + monitor 1 min.
REM Lancer dans un terminal externe (hors Cursor) en cas de timeouts.
setlocal
cd /d "%~dp0"

echo === Build S3 + workflow 1 min ===
echo.

echo 1. Build wroom-s3-test...
pio run -e wroom-s3-test
if errorlevel 1 goto err

echo 2. Workflow erase + flash + monitor 1 min...
call erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1 -SkipBuild
if errorlevel 1 goto err

echo.
echo === Fin - Verifier le dernier monitor_1min_*.log ===
goto end
:err
echo ERREUR
exit /b 1
:end
exit /b 0
