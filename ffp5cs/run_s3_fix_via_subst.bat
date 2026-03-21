@echo off
REM Build S3. Si le chemin a des espaces : lecteur virtuel P: pour eviter blocage SCons.
REM Alternative : copie miroir sans espace (run_s3_build_from_safe_path.bat, racine C:\pio-builds\ffp5cs-space-mirror).
REM Option: "workflow" = apres le build, lancer erase/flash/monitor 1 min.
setlocal
set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "RUN_WORKFLOW="
if /i "%~1"=="workflow" set "RUN_WORKFLOW=1"

echo %PROJECT_ROOT% | findstr /C:" " >nul
if errorlevel 1 (
  set "PATH_HAS_SPACE=0"
) else (
  set "PATH_HAS_SPACE=1"
)

if "%PATH_HAS_SPACE%"=="0" (
  echo === Build S3 (chemin sans espace - build direct) ===
  if defined RUN_WORKFLOW echo   + workflow erase/flash/monitor
  echo.
  cd /d "%PROJECT_ROOT%"

  echo 1. Build wroom-s3-test...
  pio run -e wroom-s3-test -v
  if errorlevel 1 exit /b 1

  if defined RUN_WORKFLOW (
    echo 2. Workflow erase + flash + monitor 1 min...
    call erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1 -SkipBuild
    if errorlevel 1 exit /b 1
    echo === Fin - Verifier le dernier monitor_1min_*.log ===
  )
  exit /b 0
)

REM --- Chemin avec espaces : subst P: ---
echo === Build S3 via lecteur virtuel P: ===
if defined RUN_WORKFLOW echo   + workflow erase/flash/monitor
echo.

subst P: /D >nul 2>&1
echo 1. Creation lecteur virtuel P: -^> projet
subst P: "%PROJECT_ROOT%"
if errorlevel 1 (
  echo ERREUR: impossible de creer subst P:
  exit /b 1
)
cd /d P:\

echo 2. Build wroom-s3-test (depuis P:\)...
pio run -e wroom-s3-test -v
set BUILD_RESULT=%errorlevel%

if defined RUN_WORKFLOW (
  echo 3. Workflow erase + flash + monitor 1 min...
  call erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1 -SkipBuild
)

echo 4. Suppression lecteur virtuel P:
subst P: /D >nul 2>&1

if %BUILD_RESULT% neq 0 (
  echo ERREUR build
  exit /b 1
)
echo === Fin ===
exit /b 0
