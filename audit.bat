@echo off
setlocal enabledelayedexpansion

set PROJECT_DIR=%~dp0..
set SRC_DIR=%PROJECT_DIR%\ffp5cs
set REPORT_DIR=%PROJECT_DIR%\audit\reports
set CONFIG_DIR=%PROJECT_DIR%\audit\config

if not exist "%REPORT_DIR%" mkdir "%REPORT_DIR%"

echo 🔍 Running cppcheck (installation globale)...
cd /d "%SRC_DIR%"
cppcheck --enable=all --inconclusive --quiet "%SRC_DIR%" --suppressions-list="%SRC_DIR%\config\cppcheck.suppress" 2> "%REPORT_DIR%\cppcheck.log"
cd /d "%~dp0"

echo 🔍 Running clang-tidy...
for /r "%SRC_DIR%" %%f in (*.cpp *.ino) do (
    echo Analyzing %%f
    clang-tidy "%%f" --config-file="%CONFIG_DIR%\clang-tidy.yaml" --quiet >> "%REPORT_DIR%\clangtidy.log" 2>&1
)

echo 📊 Measuring complexity...
lizard "%SRC_DIR%" > "%REPORT_DIR%\complexity.log"

echo 🧠 Generating AI summary...
(
    echo # Résumé d’audit automatique
    echo Ce fichier résume les résultats d’analyse et peut être donné à Cursor AI.
    echo.
    echo ## 🔹 cppcheck (code mort / bugs)
    echo ```
    type "%REPORT_DIR%\cppcheck.log" | findstr /R "warning error"
    echo ```
    echo.
    echo ## 🔹 clang-tidy (qualité & modernisation)
    echo ```
    type "%REPORT_DIR%\clangtidy.log" | findstr "warning:"
    echo ```
    echo.
    echo ## 🔹 Complexité (lizard)
    echo ```
    type "%REPORT_DIR%\complexity.log" | findstr /R "^[0-9]"
    echo ```
) > "%REPORT_DIR%\summary.md"

echo ✅ Audit terminé.
echo Rapport disponible : %REPORT_DIR%\summary.md
pause
