# Shares the game files with the NES/SNES Classic Mini over hakchi's USB
# network link. Needs administrator rights (it relaunches itself elevated).
#
#   share.ps1 [-Path <folder>] [-Name MKW]
#
# -Path is the folder that CONTAINS 3ds\mkw-3ds (default: the builder's
# build\release\sd-card). The console mounts it at /media/pc.
param(
    [string]$Path,
    [string]$Name = 'MKW'
)
$ErrorActionPreference = 'Stop'

if (-not $Path) {
    $builder = if ($env:MKW_BUILDER) { $env:MKW_BUILDER } else { 'C:\MKW-3DS-Builder' }
    $Path = Join-Path $builder 'build\release\sd-card'
}
$Path = (Resolve-Path $Path).Path
if (-not (Test-Path (Join-Path $Path '3ds\mkw-3ds'))) {
    throw "$Path doesn't contain 3ds\mkw-3ds. Make the game files first (setup.bat game-files) or pass -Path."
}

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')
if (-not $admin) {
    Write-Host "Asking for administrator rights to create the share..."
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Path', "`"$Path`"", '-Name', $Name)
    exit $p.ExitCode
}

$user = "$env:COMPUTERNAME\$env:USERNAME"
$existing = Get-SmbShare -Name $Name -ErrorAction SilentlyContinue
if ($existing -and $existing.Path -ne $Path) {
    Write-Host "Share '$Name' pointed at $($existing.Path); moving it to $Path."
    Remove-SmbShare -Name $Name -Force
    $existing = $null
}
if (-not $existing) {
    # Read/write, so saves work.
    New-SmbShare -Name $Name -Path $Path -FullAccess $user | Out-Null
}

# hakchi's link is usually a "Public" network, where Windows blocks sharing.
Get-NetConnectionProfile | Where-Object { $_.NetworkCategory -eq 'Public' } |
    Where-Object { (Get-NetIPAddress -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue).IPAddress -like '169.254.*' } |
    Set-NetConnectionProfile -NetworkCategory Private
Enable-NetFirewallRule -DisplayGroup 'File and Printer Sharing' -ErrorAction SilentlyContinue

# The hakchi kernel's CIFS module only speaks SMB1.
$restart = $false
$feat = Get-WindowsOptionalFeature -Online -FeatureName SMB1Protocol-Server -ErrorAction SilentlyContinue
if ($feat -and $feat.State -ne 'Enabled') {
    Write-Host "Turning on the SMB1 server feature (the console needs it)..."
    $r = Enable-WindowsOptionalFeature -Online -FeatureName SMB1Protocol-Server -All -NoRestart
    $restart = $r.RestartNeeded
}
Set-SmbServerConfiguration -EnableSMB1Protocol $true -Force -ErrorAction SilentlyContinue

Write-Host "`nShared $Path as \\$env:COMPUTERNAME\$Name (user $user)."
if ($restart) { Write-Host "RESTART WINDOWS once before playing, so SMB1 starts." -ForegroundColor Yellow }
