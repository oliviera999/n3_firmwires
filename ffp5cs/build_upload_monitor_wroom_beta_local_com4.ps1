# Raccourci COM4 — délégué à build_upload_monitor_wroom_beta_local.ps1
# Doc : docs/technical/WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md
# Usage : .\build_upload_monitor_wroom_beta_local_com4.ps1
#         .\build_upload_monitor_wroom_beta_local_com4.ps1 -SkipBuild

param(
    [switch]$SkipBuild
)

& "$PSScriptRoot\build_upload_monitor_wroom_beta_local.ps1" -Port COM4 @PSBoundParameters
