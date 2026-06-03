<#
.SYNOPSIS
  Test bidirectionnel panneau de controle <-> ESP32 (env test / wroom-beta-local).

.DESCRIPTION
  1. GET /ffp3/api/outputs-test/state (comme l'ESP32)
  2. POST toggle web (GPIO pompe aqua 16 ou parametre) avec auth admin
  3. Re-GET state : verifier persistance BDD (lastModifiedBy=web)
  4. Capture serie ESP : applyRemote / fetchRemoteState / code HTTP 200
  5. Optionnel : POST post-data-test simule et verification non-ecrasement web (fenetre 10s)

.EXAMPLE
  .\scripts\test_bidirectional_control_panel_local.ps1 -Port COM5 -BaseUrl http://192.168.0.158:8082
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$BaseUrl = "",
    [ValidateSet("token", "session", "both")]
    [string]$AuthMode = "both",
    [string]$AdminToken = "local_admin_token_change_me",
    [string]$AdminUsername = "admin",
    [string]$AdminPassword = "localadmin",
    [string]$ApiKey = "local_api_key_change_me",
    [int]$SerialSeconds = 120,
    [switch]$SkipSerial,
    [int]$GpioPumpAqua = 16,
    [int]$OutputId = 0
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$logsDir = Join-Path $projectDir "logs"
$captureScript = Join-Path $projectDir "tools/monitor/serial_capture.py"
$secretsFile = Join-Path $scriptDir ".beta-local-test.env"

function Import-EnvFile {
    param([string]$Path)
    $m = @{}
    if (-not (Test-Path -LiteralPath $Path)) { return $m }
    foreach ($line in Get-Content -LiteralPath $Path) {
        $t = $line.Trim()
        if ($t.Length -eq 0 -or $t.StartsWith("#")) { continue }
        $i = $t.IndexOf("=")
        if ($i -lt 1) { continue }
        $m[$t.Substring(0, $i).Trim()] = $t.Substring($i + 1).Trim().Trim('"')
    }
    return $m
}

$sec = Import-EnvFile -Path $secretsFile
if ([string]::IsNullOrWhiteSpace($BaseUrl) -and $sec.ContainsKey("N3_TEST_LOCAL_SERVER_BASE_URL")) {
    $BaseUrl = $sec["N3_TEST_LOCAL_SERVER_BASE_URL"]
}
if ($sec.ContainsKey("N3_TEST_ADMIN_TOKEN")) { $AdminToken = $sec["N3_TEST_ADMIN_TOKEN"] }
if ($sec.ContainsKey("N3_TEST_ADMIN_USERNAME")) { $AdminUsername = $sec["N3_TEST_ADMIN_USERNAME"] }
if ($sec.ContainsKey("N3_TEST_ADMIN_PASSWORD")) { $AdminPassword = $sec["N3_TEST_ADMIN_PASSWORD"] }
if ($sec.ContainsKey("N3_TEST_API_KEY")) { $ApiKey = $sec["N3_TEST_API_KEY"] }
if ([string]::IsNullOrWhiteSpace($BaseUrl)) { $BaseUrl = "http://127.0.0.1:8082" }
$BaseUrl = $BaseUrl.TrimEnd("/")

if (-not (Test-Path -LiteralPath $logsDir)) { New-Item -ItemType Directory -Path $logsDir | Out-Null }
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$reportPath = Join-Path $logsDir "bidirectional_control_$ts.md"
$serialLog = Join-Path $logsDir "bidirectional_control_$ts.log"

function Get-GpioStateFromJson {
    param($Json, [int]$Gpio)
    if ($null -eq $Json) { return $null }
    $gpioKey = [string]$Gpio
    if (-not $Json.PSObject.Properties.Name.Contains($gpioKey)) { return $null }
    $entry = $Json.$gpioKey
    if ($null -eq $entry) { return $null }
    if ($entry -is [int] -or $entry -is [long] -or $entry -is [double]) { return [int]$entry }
    if ($entry.PSObject.Properties.Name -contains "state") { return [int]$entry.state }
    return $null
}

function Get-OutputIdForGpio {
    param([int]$Gpio, [int]$ExplicitId)
    if ($ExplicitId -gt 0) { return $ExplicitId }
    $repoRoot = Split-Path (Split-Path (Split-Path $scriptDir -Parent) -Parent) -Parent
    $compose = Join-Path $repoRoot "serveur\docker-compose.local.yml"
    if (-not (Test-Path -LiteralPath $compose)) { return $null }
    try {
        $q = "SELECT id FROM iot_n3_local.ffp3Outputs2 WHERE gpio=$Gpio LIMIT 1;"
        $out = docker compose -f $compose exec -T db mysql -uroot -proot_local_iot_n3 -N -e $q 2>$null
        if ($out -match '^\s*(\d+)\s*$') { return [int]$Matches[1] }
    } catch { }
    return $null
}

function New-AdminWebSession {
    param([string]$Url, [string]$User, [string]$Pass)
    $session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $loginPage = Invoke-WebRequest -Uri "$Url/login" -WebSession $session -UseBasicParsing
    $csrf = ""
    if ($loginPage.Content -match 'name="_csrf_token"\s+value="([^"]+)"') { $csrf = $Matches[1] }
    $body = @{ username = $User; password = $Pass; _csrf_token = $csrf }
    Invoke-WebRequest -Uri "$Url/login" -Method POST -Body $body -WebSession $session -UseBasicParsing | Out-Null
    $probe = Invoke-WebRequest -Uri "$Url/aquaponie-control-test" -WebSession $session -UseBasicParsing
    if ($probe.Content -match '<title>Connexion') { throw "Echec login session ($User)" }
    return $session
}

function Test-HttpOk {
    param(
        [string]$Url,
        [hashtable]$Headers = @{},
        [string]$Method = "GET",
        [object]$Body = $null,
        [switch]$UseAdminSession
    )
    try {
        $params = @{
            Uri             = $Url
            Method          = $Method
            Headers         = $Headers
            TimeoutSec      = 15
            UseBasicParsing = $true
        }
        if ($UseAdminSession -and $script:AdminSession) { $params["WebSession"] = $script:AdminSession }
        if ($null -ne $Body) {
            if ($Body -is [hashtable]) {
                $params["Body"] = ($Body.GetEnumerator() | ForEach-Object { "{0}={1}" -f $_.Key, $_.Value }) -join "&"
                $params["ContentType"] = "application/x-www-form-urlencoded"
            } else {
                $params["Body"] = $Body
            }
        }
        $r = Invoke-WebRequest @params
        return @{ ok = $true; status = $r.StatusCode; content = $r.Content }
    } catch {
        $status = 0
        if ($_.Exception.Response) { $status = [int]$_.Exception.Response.StatusCode }
        return @{ ok = $false; status = $status; content = $_.Exception.Message }
    }
}

$lines = @()
$lines += "# Rapport test bidirectionnel panneau / ESP"
$lines += ""
$lines += "- Date : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += "- BaseUrl : $BaseUrl"
$lines += "- Port serie : $Port"
$lines += "- AuthMode : $AuthMode"
$lines += ""

$script:AdminSession = $null
if ($AuthMode -eq "session" -or $AuthMode -eq "both") {
    try {
        $script:AdminSession = New-AdminWebSession -Url $BaseUrl -User $AdminUsername -Pass $AdminPassword
        $lines += "- Session admin : OK"
    } catch {
        $lines += "- Session admin : ECHEC ($($_.Exception.Message))"
        if ($AuthMode -eq "session") {
            $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
            Write-Host "Login session impossible. Rapport : $reportPath" -ForegroundColor Red
            exit 2
        }
    }
}

# 0. Ping serveur
$ping = Test-HttpOk -Url "$BaseUrl/ping"
$lines += "## 0. Disponibilite serveur"
$lines += "- GET /ping : $(if ($ping.ok) { "OK $($ping.status)" } else { "ECHEC ($($ping.content))" })"
if (-not $ping.ok) {
    $lines += ""
    $lines += "**Bloquant** : demarrer la stack Docker (`serveur/tools/local-docker.ps1 up`) et verifier l'IP dans `local_server_overrides.h`."
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    Write-Host "Serveur injoignable. Rapport : $reportPath" -ForegroundColor Red
    exit 2
}

# 1. Etat initial (API device, sans auth admin — comme ESP32)
$stateUrl = "$BaseUrl/ffp3/api/outputs-test/state"
$state1 = Test-HttpOk -Url $stateUrl
$lines += ""
$lines += "## 1. Serveur -> lecture (GET outputs-test/state)"
$lines += "- URL : $stateUrl"
$lines += "- Statut : $(if ($state1.ok) { $state1.status } else { "echec" })"
$json1 = $null
$pumpId = $null
$pumpStateBefore = $null
if ($state1.ok) {
    $json1 = $state1.content | ConvertFrom-Json
    $pumpStateBefore = Get-GpioStateFromJson -Json $json1 -Gpio $GpioPumpAqua
    $gpioKey = [string]$GpioPumpAqua
    $entry = $json1.$gpioKey
    if ($entry -and $entry.PSObject.Properties.Name -contains "id") { $pumpId = [int]$entry.id }
    if (-not $pumpId) { $pumpId = Get-OutputIdForGpio -Gpio $GpioPumpAqua -ExplicitId $OutputId }
    $lines += "- GPIO $GpioPumpAqua avant toggle : id=$pumpId state=$pumpStateBefore (JSON plat ou objet)"
} else {
    $lines += "- Erreur : $($state1.content)"
}

# 2. Panneau web -> serveur (toggle)
$lines += ""
$lines += "## 2. Panneau -> serveur (POST toggle)"
$toggle = @{ ok = $false }
if ($null -eq $pumpStateBefore) { $pumpStateBefore = 0 }
$newState = if ($pumpStateBefore -eq 1) { 0 } else { 1 }
if (-not $pumpId) {
    $lines += "- **SKIP toggle** : id sortie BDD introuvable pour GPIO $GpioPumpAqua (docker mysql init ou -OutputId)."
} else {
    $toggleUrl = "$BaseUrl/ffp3/api/outputs-test/toggle"
    $toggleBody = @{ id = $pumpId; state = $newState; gpio = $GpioPumpAqua }
    $toggleHeaders = @{}
    if (($AuthMode -eq "token" -or $AuthMode -eq "both") -and -not $script:AdminSession) {
        $toggleHeaders["X-Admin-Token"] = $AdminToken
    }
    $toggle = Test-HttpOk -Url $toggleUrl -Method "POST" -Headers $toggleHeaders -Body $toggleBody -UseAdminSession
    $lines += "- POST $toggleUrl id=$pumpId state=$newState gpio=$GpioPumpAqua"
    $lines += "- Resultat : $(if ($toggle.ok) { "OK $($toggle.status) $($toggle.content)" } else { "ECHEC $($toggle.content)" })"
}

# 3. Persistance serveur (re-GET)
$lines += ""
$lines += "## 3. Persistance cote serveur (re-GET)"
$persistOk = $false
Start-Sleep -Seconds 1
$state2 = Test-HttpOk -Url "$stateUrl`?fresh=1"
$pumpStateAfter = $null
if ($state2.ok) {
    $json2 = $state2.content | ConvertFrom-Json
    $pumpStateAfter = Get-GpioStateFromJson -Json $json2 -Gpio $GpioPumpAqua
    $lines += "- GPIO $GpioPumpAqua apres toggle : state=$pumpStateAfter (attendu $newState)"
    $persistOk = ($null -ne $pumpStateAfter) -and ($pumpStateAfter -eq $newState)
    $lines += "- Persistance JSON : $(if ($persistOk) { 'OK' } else { 'KO' })"
} else {
    $lines += "- Re-GET echoue (HTTP $($state2.status)) : $($state2.content)"
}

# 4. Capture serie (ESP32 poll + apply)
$lines += ""
$lines += "## 4. ESP32 (serie) : application commande distante"
if (-not $SkipSerial) {
    . (Join-Path $scriptDir "Release-ComPort.ps1")
    Release-ComPortIfNeeded -Port $Port
    Start-Sleep -Seconds 2
    Push-Location $projectDir
    try {
        python $captureScript --port $Port --baud 115200 --duration $SerialSeconds --out $serialLog --quiet
    } catch {
        $lines += "- Capture serie : ECHEC ($($_.Exception.Message))"
    } finally { Pop-Location }
    if (Test-Path -LiteralPath $serialLog) {
        $log = Get-Content -LiteralPath $serialLog -Raw
        $hasGet = $log -match "outputs-test/state"
        $has200 = $log -match "(?i)code.?=.?200|HTTP.*200"
        $hasApply = $log -match "(?i)applyRemote|fetchRemoteState|configSynced|remote"
        $hasPost = $log -match "post-data-test"
        $lines += "- Log : $serialLog"
        $lines += "- Lignes GET outputs-test : $(if ($hasGet) { 'oui' } else { 'non' })"
        $lines += "- HTTP 200 observe : $(if ($has200) { 'oui' } else { 'non' })"
        $lines += "- Traces apply/fetch remote : $(if ($hasApply) { 'oui' } else { 'non' })"
        $lines += "- POST post-data-test : $(if ($hasPost) { 'oui' } else { 'non' })"
        $bidirEsp = $hasGet -and $has200 -and $hasApply
        $lines += "- **Sens serveur->ESP (poll + apply)** : $(if ($bidirEsp) { 'OK (indices logs)' } else { 'KO ou incomplet' })"
    }
} else {
    $lines += "- Capture serie ignoree (--SkipSerial)"
}

# 5. Synthese
$lines += ""
$lines += "## 5. Synthese"
$toggleOk = ($null -ne $toggle) -and $toggle.ok
$webOk = $ping.ok -and $state1.ok -and (-not $pumpId -or ($toggleOk -and $persistOk))
$lines += "- Panneau -> BDD (toggle + re-GET) : $(if ($webOk) { 'OK' } else { 'a verifier' })"
$lines += "- BDD -> ESP (GET + apply, logs) : voir section 4"
$lines += "- ESP -> serveur (POST mesures) : chercher post-data-test + insertion ffp3Data2 (suite integration Docker)"
$lines += ""
$lines += "Reference : serveur/docs/SYNCHRONISATION_BIDIRECTIONNELLE.md (fenetre priorite web 10s)."

$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Rapport : $reportPath" -ForegroundColor Cyan
if (-not $ping.ok) { exit 2 }
if (-not $webOk) { exit 1 }
exit 0
