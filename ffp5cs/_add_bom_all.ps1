# Add UTF-8 BOM to all .ps1 in current dir that don't have it
$bom = [System.Text.Encoding]::UTF8.GetPreamble()
Get-ChildItem -Filter "*.ps1" -File | ForEach-Object {
  $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
  if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
    [System.IO.File]::WriteAllBytes($_.FullName, $bom + $bytes)
    Write-Host "BOM added: $($_.Name)"
  }
}
