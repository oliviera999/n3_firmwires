@echo off
REM Build wroom-s3-test. Si le chemin du projet contient des espaces, copie vers BUILD_ROOT (sans espace).
REM Si le chemin n'a pas d'espace : build direct depuis le projet (pas de copie).
REM Option: "workflow" = apres le build, lancer erase/flash/monitor.
setlocal
set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
if not defined BUILD_ROOT set "BUILD_ROOT=C:\ffp5cs_build"
set "RUN_WORKFLOW="
if /i "%~1"=="workflow" set "RUN_WORKFLOW=1"

echo %PROJECT_ROOT% | findstr /C:" " >nul
if errorlevel 1 (
  set "PATH_HAS_SPACE=0"
) else (
  set "PATH_HAS_SPACE=1"
)

if "%PATH_HAS_SPACE%"=="0" (
  echo === Build S3 (chemin sans espace - build direct depuis le projet) ===
  echo   Projet : %PROJECT_ROOT%
  if defined RUN_WORKFLOW echo   + workflow erase/flash/monitor
  echo.

  echo 1. Patch plateforme...
  cd /d "%PROJECT_ROOT%"
  python "%~dp0tools\patch_espressif32_custom_sdkconfig_only.py"
  if errorlevel 1 exit /b 1

  echo 2. Suppression sdkconfig racine package (forcer recompil libs IWDT)...
  del /q "C:\Users\olivi\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig" 2>nul

  echo 3. Build wroom-s3-test (15-25 min possible si recompile libs)...
  pio run -e wroom-s3-test -v
  if errorlevel 1 exit /b 1

  if defined RUN_WORKFLOW (
    echo 4. Workflow erase / flash / monitor 1 min...
    powershell -ExecutionPolicy Bypass -File ".\erase_flash_fs_monitor_5min_analyze.ps1" -Environment wroom-s3-test -Port COM7 -DurationMinutes 1
    if errorlevel 1 exit /b 1
    echo === Fin - Verifier le dernier monitor_1min_*.log ===
  ) else (
    echo === Build OK. Pour flash+monitor : run_s3_build_from_safe_path.bat workflow ===
  )
  exit /b 0
)

REM --- Chemin avec espaces : copie vers BUILD_ROOT ---
echo === Build S3 depuis chemin sans espace (copie vers %BUILD_ROOT%) ===
echo   Projet  : %PROJECT_ROOT%
echo   Build dans : %BUILD_ROOT%
if defined RUN_WORKFLOW echo   + workflow erase/flash/monitor depuis %BUILD_ROOT%
echo.

echo 1. Patch plateforme...
python "%~dp0tools\patch_espressif32_custom_sdkconfig_only.py"
if errorlevel 1 exit /b 1

echo 2. Copie du projet vers %BUILD_ROOT%...
if not exist "%BUILD_ROOT%" mkdir "%BUILD_ROOT%"
xcopy "%PROJECT_ROOT%\*" "%BUILD_ROOT%\" /E /I /H /Y
if errorlevel 1 (
  echo ERREUR: copie vers %BUILD_ROOT% impossible
  exit /b 1
)

echo 3. Suppression sdkconfig racine package (forcer recompil libs IWDT)...
del /q "C:\Users\olivi\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig" 2>nul

echo 4. Build wroom-s3-test (depuis %BUILD_ROOT% - 15-25 min possible)...
cd /d "%BUILD_ROOT%"
pio run -e wroom-s3-test -v
set BUILD_RESULT=%errorlevel%

if %BUILD_RESULT% neq 0 (
  echo ERREUR build
  exit /b %BUILD_RESULT%
)

echo 5. Recopie firmware et partition vers le projet...
set "BUILD_DIR=%BUILD_ROOT%\.pio\build\wroom-s3-test"
set "DEST_DIR=%PROJECT_ROOT%\.pio\build\wroom-s3-test"
if exist "%BUILD_DIR%\firmware.bin" (
  if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"
  copy /Y "%BUILD_DIR%\firmware.bin" "%DEST_DIR%\"
  copy /Y "%BUILD_DIR%\partitions.bin" "%DEST_DIR%\" 2>nul
  echo    firmware.bin + partitions.bin copies dans le projet.
) else (
  echo    [INFO] Dossier build non trouve, recopie manuelle si besoin.
)

echo.
if defined RUN_WORKFLOW (
  echo 6. Workflow erase / flash / monitor 1 min depuis %BUILD_ROOT%...
  cd /d "%BUILD_ROOT%"
  powershell -ExecutionPolicy Bypass -File ".\erase_flash_fs_monitor_5min_analyze.ps1" -Environment wroom-s3-test -Port COM7 -DurationMinutes 1
  if errorlevel 1 exit /b 1
  echo === Fin - Verifier le dernier monitor_1min_*.log dans %BUILD_ROOT% ===
) else (
  echo === Build OK ===
  echo Lancer le workflow depuis le dossier de build : run_s3_build_from_safe_path.bat workflow
)
exit /b 0
