#Requires -Version 5.1
<##
Doorlight Gaming Mode watcher for Windows 10/11.
Requires NO additional Python or games-specific SDK. Detects only configured
executable names using Get-Process, not window titles or substring searches.

  .\Watch-DoorlightGames.ps1 -Setup    # DPAPI-protect your API key
  .\Watch-DoorlightGames.ps1 -Once     # report detected games without changes
  .\Watch-DoorlightGames.ps1           # monitor and send leased gaming status

Only sends commands to the NEW Doorlight ESP32 at /api/gaming. Does not touch
existing fingerprint lock or bedroom LED-strip firmware.
##>
[CmdletBinding()]
param(
    [switch] $Setup,
    [switch] $Once,
    [string] $ConfigPath = (Join-Path $PSScriptRoot 'games.json')
)
$ErrorActionPreference = 'Stop'
$secretDir = Join-Path $env:LOCALAPPDATA 'Doorlight'
$secretFile = Join-Path $secretDir 'api-key.dpapi'

function Get-ApiKey {
    if (!(Test-Path -LiteralPath $secretFile)) {
        throw 'No API key configured. Run this script with -Setup first.'
    }
    $secure = (Get-Content -LiteralPath $secretFile -Raw).Trim() | ConvertTo-SecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}

if ($Setup) {
    if (!(Test-Path -LiteralPath $secretDir)) {
        New-Item -ItemType Directory -Path $secretDir | Out-Null
    }
    $key = Read-Host 'Paste the Doorlight X-API-Key' -AsSecureString
    # Windows DPAPI binds this encrypted file to the current Windows user.
    ConvertFrom-SecureString $key | Set-Content -LiteralPath $secretFile -Encoding ASCII
    Write-Host "Encrypted API key saved to $secretFile under your Windows account."
    exit 0
}
if (!(Test-Path -LiteralPath $ConfigPath)) {
    throw "Missing games.json. Copy games.example.json to games.json and edit it: $ConfigPath"
}
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$baseUrl = ([string]$config.doorlightUrl).TrimEnd('/')
if ($baseUrl -notmatch '^http://[a-zA-Z0-9.-]+(?::\d+)?$') {
    throw 'doorlightUrl must be a local HTTP hostname/IP, without a path.'
}
$poll = [math]::Max(2, [math]::Min(30, [int]$config.pollSeconds))
$startWait = [math]::Max(0, [math]::Min(120, [int]$config.startAfterSeconds))
$stopWait = [math]::Max(5, [math]::Min(300, [int]$config.stopAfterSeconds))
$heartbeat = [math]::Max(10, [math]::Min(45, [int]$config.heartbeatSeconds))
$lease = [math]::Max(60, [math]::Min(600, [int]$config.leaseSeconds))
if ($heartbeat -ge $lease/2) { throw 'heartbeatSeconds must be less than half the lease.' }
$names = @{}
foreach ($game in @($config.games)) {
    foreach ($exe in @($game.executables)) {
        $name = ([string]$exe).Trim().ToLowerInvariant()
        if ($name -match '^[a-z0-9][a-z0-9_. -]*\.exe$' -and $name -notlike 'your-*') {
            $names[$name] = [string]$game.name
        }
    }
}
if ($names.Count -eq 0) { throw 'No valid executable names in games.json.' }

function Get-RunningGame {
    $processes = @(Get-Process -ErrorAction SilentlyContinue)
    foreach ($p in $processes) {
        $key = ($p.ProcessName + '.exe').ToLowerInvariant()
        if ($names.ContainsKey($key)) { return [string]$names[$key] }
    }
    return ''
}
if ($Once) {
    $game = Get-RunningGame
    if ($game) { Write-Host "Found configured game: $game" }
    else { Write-Host 'No configured game running.' }
    exit 0
}

$key = Get-ApiKey
$headers = @{ 'X-API-Key' = $key }
function Get-RemoteStatus {
    return Invoke-RestMethod -Uri "$baseUrl/api/routines" -Headers $headers -Method Get -TimeoutSec 4
}
function Set-RemoteGaming([bool]$active) {
    $payload = @{ active = $active; source = 'pc-watcher'; leaseSeconds = $lease } | ConvertTo-Json -Compress
    return Invoke-RestMethod -Uri "$baseUrl/api/gaming" -Headers $headers -Method Post -ContentType 'application/json' -Body $payload -TimeoutSec 4
}
$owns = $false
$seenSince = $null
$absentSince = $null
$lastHeartbeat = [datetime]::MinValue
Write-Host "Doorlight Gaming Watcher · $baseUrl · $($names.Count) configured executable(s)"
Write-Host 'Press Ctrl+C to exit. If the watcher stops unexpectedly, the ESP32 lease expires automatically.'
try {
    while ($true) {
        $now = Get-Date
        $game = Get-RunningGame
        if ($game) { $absentSince = $null; if ($null -eq $seenSince) { $seenSince = $now } }
        else { $seenSince = $null; if ($null -eq $absentSince) { $absentSince = $now } }
        try {
            if ($game -and (($now - $seenSince).TotalSeconds -ge $startWait)) {
                if (!$owns) {
                    $remote = Get-RemoteStatus
                    if (!$remote.gamingActive -or $remote.gamingSource -eq 'pc-watcher') {
                        Set-RemoteGaming $true | Out-Null
                        $owns = $true
                        $lastHeartbeat = $now
                        Write-Host "$(Get-Date -Format T) Gaming started: $game"
                    } else {
                        Write-Verbose 'Manual Gaming Mode in use. Watcher will not take ownership.'
                    }
                }
            }
            if ($owns -and $game -and (($now - $lastHeartbeat).TotalSeconds -ge $heartbeat)) {
                Set-RemoteGaming $true | Out-Null
                $lastHeartbeat = $now
                Write-Verbose 'Renewed Gaming Mode lease.'
            }
            if ($owns -and !$game -and (($now - $absentSince).TotalSeconds -ge $stopWait)) {
                Set-RemoteGaming $false | Out-Null
                $owns = $false
                Write-Host "$(Get-Date -Format T) Gaming stopped; previous bulb scene restored."
            }
        } catch {
            # On transient failure, keep ownership and retry next poll. If PC
            # cannot recover, the ESP32's own lease watchdog releases the mode.
            Write-Warning "Doorlight connection failed: $($_.Exception.Message)"
        }
        Start-Sleep -Seconds $poll
    }
} finally {
    if ($owns) {
        try { Set-RemoteGaming $false | Out-Null } catch { Write-Warning 'Could not send final stop; the ESP32 lease will expire.' }
    }
}
