# Capture 35 s de sortie serie vers .cursor\debug.log (pour verification post-fix)
$proj = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$logPath = Join-Path $proj ".cursor\debug.log"
if (Test-Path $logPath) { Remove-Item $logPath -Force }
$p = Start-Process -FilePath "pio" -ArgumentList "device","monitor" -WorkingDirectory $proj -PassThru -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError (Join-Path $proj ".cursor\debug_err.log")
Start-Sleep -Seconds 35
if ($p -and !$p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
Write-Host "Capture terminee. Log: $logPath"
