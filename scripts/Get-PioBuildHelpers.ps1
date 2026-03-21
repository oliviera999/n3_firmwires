# =============================================================================
# Helpers chemins de build PlatformIO (projet n3 IoT)
# =============================================================================
# Aligné sur firmwires/scripts/pio_redirect_build_dir.py :
# - Redirection active si (Windows ET pas N3_PIO_BUILD_REDIRECT=0) OU N3_PIO_BUILD_ROOT défini.
# - Racine par défaut Windows : C:\pio-builds
# - Artefacts : <racine>\<slug>\<env>\firmware.bin (etc.)
#
# Usage (dot-source) :
#   . (Join-Path $root "firmwires\scripts\Get-PioBuildHelpers.ps1")
#   Get-N3PioFirmwareBin -ProjectRoot $proj -Environment "wroom-test"
# =============================================================================

function Get-N3PioRedirectRoot {
    $flag = $env:N3_PIO_BUILD_REDIRECT
    if ($flag -and $flag.Trim().ToLowerInvariant() -in @("0", "false", "no", "off")) {
        return $null
    }
    $custom = $env:N3_PIO_BUILD_ROOT
    if ($custom -and $custom.Trim()) {
        return $custom.Trim()
    }
    $isWindows = $false
    if ($PSVersionTable.PSVersion.Major -ge 6 -and $null -ne (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue) -and $IsWindows) {
        $isWindows = $true
    } elseif ($env:OS -match 'Windows') {
        $isWindows = $true
    }
    if ($isWindows) {
        if (-not $flag -or $flag.Trim().ToLowerInvariant() -notin @("0", "false", "no", "off")) {
            return "C:\pio-builds"
        }
    }
    return $null
}

function Get-N3PioProjectSlug {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )
    $leaf = Split-Path -Path $ProjectRoot -Leaf
    return ($leaf -split '\s+') -join '-'
}

function Test-N3PioRedirectActive {
    return $null -ne (Get-N3PioRedirectRoot)
}

function Get-N3PioEnvBuildDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$Environment
    )
    $proj = [System.IO.Path]::GetFullPath($ProjectRoot)
    $redirect = Get-N3PioRedirectRoot
    if ($redirect) {
        $slug = Get-N3PioProjectSlug -ProjectRoot $proj
        return [System.IO.Path]::Combine($redirect, $slug, $Environment)
    }
    return [System.IO.Path]::Combine($proj, ".pio", "build", $Environment)
}

function Get-N3PioFirmwareBin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$Environment
    )
    $redirectPath = $null
    $r = Get-N3PioRedirectRoot
    if ($r) {
        $slug = Get-N3PioProjectSlug -ProjectRoot ([System.IO.Path]::GetFullPath($ProjectRoot))
        $redirectPath = [System.IO.Path]::Combine($r, $slug, $Environment, "firmware.bin")
    }
    $classic = [System.IO.Path]::Combine([System.IO.Path]::GetFullPath($ProjectRoot), ".pio", "build", $Environment, "firmware.bin")
    if ($redirectPath -and (Test-Path -LiteralPath $redirectPath)) {
        return $redirectPath
    }
    if (Test-Path -LiteralPath $classic) {
        return $classic
    }
    if ($redirectPath) { return $redirectPath }
    return $classic
}

function Get-N3PioLittlefsBin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$Environment
    )
    $redirectPath = $null
    $r = Get-N3PioRedirectRoot
    if ($r) {
        $slug = Get-N3PioProjectSlug -ProjectRoot ([System.IO.Path]::GetFullPath($ProjectRoot))
        $redirectPath = [System.IO.Path]::Combine($r, $slug, $Environment, "littlefs.bin")
    }
    $classic = [System.IO.Path]::Combine([System.IO.Path]::GetFullPath($ProjectRoot), ".pio", "build", $Environment, "littlefs.bin")
    if ($redirectPath -and (Test-Path -LiteralPath $redirectPath)) {
        return $redirectPath
    }
    if (Test-Path -LiteralPath $classic) {
        return $classic
    }
    if ($redirectPath) { return $redirectPath }
    return $classic
}

function Get-N3PioVersionTxt {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$Environment
    )
    $binDir = Split-Path -Parent (Get-N3PioFirmwareBin -ProjectRoot $ProjectRoot -Environment $Environment)
    return [System.IO.Path]::Combine($binDir, "version.txt")
}

function Get-N3PioFirmwareElf {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$Environment
    )
    $dir = Split-Path -Parent (Get-N3PioFirmwareBin -ProjectRoot $ProjectRoot -Environment $Environment)
    return [System.IO.Path]::Combine($dir, "firmware.elf")
}

function Write-N3PioWorkspaceAdvice {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )
    $full = [System.IO.Path]::GetFullPath($ProjectRoot)
    if ($full -match '\s') {
        Write-Host "N3 build: le chemin du projet contient des espaces - risque PlatformIO/SCons. Preferez un clone court ou N3_PIO_BUILD_ROOT." -ForegroundColor Yellow
    }
    if ($full.Length -gt 200) {
        Write-Host ('N3 build: chemin tres long ({0} caracteres) - preferez C:\pio-builds via redirection (Windows) ou deplacer le depot.' -f $full.Length) -ForegroundColor Yellow
    }
    $r = Get-N3PioRedirectRoot
    if ($r) {
        Write-Host "N3 build: redirection active, racine = $r (desactiver: `$env:N3_PIO_BUILD_REDIRECT='0')" -ForegroundColor Gray
    }
}

function Assert-N3PioProjectRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )
    $ini = Join-Path $ProjectRoot "platformio.ini"
    if (-not (Test-Path -LiteralPath $ini)) {
        throw "platformio.ini introuvable sous : $ProjectRoot"
    }
}
