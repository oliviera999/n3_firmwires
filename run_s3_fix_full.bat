@echo off
REM Lancer ce script dans un terminal externe (hors Cursor) pour eviter les timeouts.
REM Corrige le boot loop TG1WDT sur ESP32-S3.
setlocal
cd /d "%~dp0"

echo === Correction boot loop S3 TG1WDT ===
echo.

echo 1. Patch plateforme...
python tools/patch_espressif32_custom_sdkconfig_only.py
if errorlevel 1 goto err

echo 2. Patch sdkconfig package Arduino (sans sdkconfig racine pour eviter reinstall)...
python tools/patch_arduino_libs_s3_wdt.py

echo 3. Suppression sdkconfig racine si present (force call_compile_libs sans reinstall)...
del /q "%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig" 2>nul

echo 4. Build wroom-s3-test (peut prendre 15-20 min si recompile lib Arduino)...
pio run -e wroom-s3-test > build_s3_full.log 2>&1
if errorlevel 1 goto err

echo 5. Recherche "Compile Arduino IDF" ou "Add:" ou "Replace:" dans le log...
findstr /i "Compile Arduino IDF Add: Replace:" build_s3_full.log >nul
if errorlevel 1 (
  echo    [ATTENTION] Lib Arduino peut-etre pas recompilee - boot loop possible
) else (
  echo    [OK] Lib Arduino recompilee avec IWDT 300s
)

echo 6. Workflow erase + flash + monitor 1 min...
call erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1
if errorlevel 1 goto err

echo.
echo === Fin - Verifier le dernier monitor_1min_*.log pour BOOT FFP5CS ===
goto end
:err
echo ERREUR
exit /b 1
:end
exit /b 0
