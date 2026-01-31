# Add UTF-8 BOM to .ps1 files so PowerShell 5.1 parses them correctly
$bom = [System.Text.Encoding]::UTF8.GetPreamble()
$scripts = @(
  'check_monitoring.ps1','sync_ffp3distant.ps1','install_cppcheck.ps1',
  'diagnostic_communication_serveur.ps1','diagnostic_serveur_distant.ps1',
  'check_emails_from_log.ps1','analyze_log_exhaustive.ps1','generate_diagnostic_report.ps1',
  'check_monitoring_status.ps1','sync_all.ps1','wait_and_analyze_monitoring.ps1'
)
foreach ($f in $scripts) {
  if (Test-Path $f) {
    $path = (Resolve-Path $f).Path
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
      $newBytes = $bom + $bytes
      [System.IO.File]::WriteAllBytes($path, $newBytes)
      Write-Host "BOM added: $f"
    }
  }
}
