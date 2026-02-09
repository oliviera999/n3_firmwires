@echo off
REM Build S3. Si le chemin a des espaces : lecteur virtuel P: pour eviter blocage SCons.
REM Si le chemin n'a pas d'espace : build direct depuis le projet.
REM Option: "quick" = ne pas forcer la recompil des libs (build rapide, boot loop IWDT reste).
setlocal
set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "SKIP_RECOMPILE="
if /i "%~1"=="quick" set "SKIP_RECOMPILE=1"

echo %PROJECT_ROOT% | findstr /C:" " >nul
if errorlevel 1 (
  set "PATH_HAS_SPACE=0"
) else (
  set "PATH_HAS_SPACE=1"
)

if "%PATH_HAS_SPACE%"=="0" (
  echo === Build S3 (chemin sans espace - build direct) ===
  if defined SKIP_RECOMPILE echo [MODE RAPIDE - recompil libs desactivee - boot loop IWDT possible]
  echo.
  cd /d "%PROJECT_ROOT%"

  echo 1. Patch plateforme...
  python tools\patch_espressif32_custom_sdkconfig_only.py
  if errorlevel 1 exit /b 1

  echo 2. Patch sdkconfig package Arduino...
  python tools\patch_arduino_libs_s3_wdt.py

  echo 3. Sdkconfig package...
  if defined SKIP_RECOMPILE (
    echo. > "C:\Users\olivi\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig"
  ) else (
    del /q "C:\Users\olivi\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig" 2>nul
  )

  echo 4. Build wroom-s3-test...
  pio run -e wroom-s3-test -v
  if errorlevel 1 exit /b 1

  echo 5. Workflow erase + flash + monitor 1 min...
  call erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1
  echo === Fin - Verifier le dernier monitor_1min_*.log pour BOOT FFP5CS ===
  exit /b 0
)

REM --- Chemin avec espaces : subst P: ---
echo === Build S3 via lecteur virtuel P: (evite espaces dans le chemin) ===
if defined SKIP_RECOMPILE echo [MODE RAPIDE - recompil libs Arduino desactivee - boot loop IWDT possible]
echo.

subst P: /D >nul 2>&1
echo 1. Creation lecteur virtuel P: -> projet
subst P: "%PROJECT_ROOT%"
if errorlevel 1 (
  echo ERREUR: impossible de creer subst P:
  exit /b 1
)
cd /d P:\

echo 2. Patch plateforme...
python tools\patch_espressif32_custom_sdkconfig_only.py
if errorlevel 1 goto cleanup

echo 3. Patch sdkconfig package Arduino...
python tools\patch_arduino_libs_s3_wdt.py

echo 4. Sdkconfig package...
if defined SKIP_RECOMPILE (
  echo. > "C:\Users\olivi\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig"
) else (
  del /q "C:\Users\olivi\.platformio\packages\framework-arduinoespressif32-libs\sdkconfig" 2>nul
)

echo 5. Build wroom-s3-test (depuis P:\ - mode verbose)...
pio run -e wroom-s3-test -v
set BUILD_RESULT=%errorlevel%
if %BUILD_RESULT% neq 0 goto cleanup

echo 6. Workflow erase + flash + monitor 1 min...
call erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1

:cleanup
echo 7. Suppression lecteur virtuel P:
subst P: /D >nul 2>&1

if %BUILD_RESULT% neq 0 (
  echo ERREUR build
  exit /b 1
)
echo === Fin - Verifier le dernier monitor_1min_*.log pour BOOT FFP5CS ===
exit /b 0
