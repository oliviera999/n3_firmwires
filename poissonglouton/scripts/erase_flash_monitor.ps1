param(
  [string]$Environment = "pgl-s3-headless",
  [string]$Port = "COM4",
  [int]$DurationSeconds = 180
)

Write-Host "[PGL] Erase flash ($Environment) sur $Port"
pio run -e $Environment -t erase --upload-port $Port

Write-Host "[PGL] Upload firmware ($Environment)"
pio run -e $Environment -t upload --upload-port $Port

Write-Host "[PGL] Monitor $DurationSeconds s"
pio device monitor -e $Environment --port $Port
