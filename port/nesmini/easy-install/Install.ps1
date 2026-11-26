# Easy installer: puts the Mario Kart Wii menu entry on a hakchi CE NES/SNES
# Classic Mini over the USB link and sets up auto-mounting of the PC share.
#   Install.ps1 [-Console 169.254.1.1] [-PcUser you] [-PcPass secret] [-Share MKW]
# Works from the release zip (files\ next to this script) or from the repo.
param([string]$Console, [string]$PcUser, [string]$PcPass, [string]$Share = 'MKW')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ssh = 'ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -o BatchMode=yes -o LogLevel=ERROR'

Write-Host "== Mario Kart Wii (NES Mini port) menu entry installer ==`n"
if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
    throw "Windows OpenSSH client not found. Install it: Settings > Apps > Optional features > OpenSSH Client."
}

# 1. Find the console on hakchi's USB network link.
if (-not $Console) {
    $cands = @('169.254.1.1', '169.254.13.37') + (Get-NetNeighbor -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -like '169.254.*' -and $_.State -in 'Reachable','Stale' -and $_.IPAddress -ne '169.254.255.255' } |
        Select-Object -ExpandProperty IPAddress)
    foreach ($ip in ($cands | Select-Object -Unique)) {
        Write-Host "Looking for the console at $ip..."
        if ((Test-NetConnection $ip -Port 22 -WarningAction SilentlyContinue).TcpTestSucceeded) { $Console = $ip; break }
    }
    if (-not $Console) { throw "Console not found. Plug it in with hakchi's USB cable, turn it on, and try again." }
}
Write-Host "Console found at $Console.`n"

# 2. The PC's address on the same link (the console mounts the share from it).
$pcIp = (Find-NetRoute -RemoteIPAddress $Console | Where-Object IPAddress | Select-Object -First 1).IPAddress

# 3. Share login (a Windows account on this PC that can read the share).
if (-not $PcUser) { $PcUser = Read-Host "Windows user for the '$Share' share [$env:USERNAME]"; if (-not $PcUser) { $PcUser = $env:USERNAME } }
if (-not $PcPass) { $sec = Read-Host "Password for $PcUser" -AsSecureString
$PcPass = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)) }
if ($PcPass -match "'" -or $PcUser -match "'") { throw "The user name and password can't contain a ' character." }

# 4. Menu icon: fetch the official cover from GameTDB at install time (it isn't
#    shipped in this package), scaled to fit hakchi's 228x204 box. Falls back to
#    the generic icon in files\ when offline.
$icon = Join-Path $here 'files\icon.png'
try {
    Write-Host "`nDownloading the official cover from GameTDB..."
    $raw = Join-Path $env:TEMP 'mkw-cover-RMCE01.png'
    Invoke-WebRequest 'https://art.gametdb.com/wii/cover/US/RMCE01.png' -OutFile $raw -UseBasicParsing -TimeoutSec 20
    Add-Type -AssemblyName System.Drawing
    $src = [Drawing.Image]::FromFile($raw)
    $k = [Math]::Min(228 / $src.Width, 204 / $src.Height)
    $w = [int]($src.Width * $k); $h = [int]($src.Height * $k)
    $dst = New-Object Drawing.Bitmap $w, $h
    $g = [Drawing.Graphics]::FromImage($dst); $g.InterpolationMode = 'HighQualityBicubic'
    $g.DrawImage($src, 0, 0, $w, $h); $g.Dispose(); $src.Dispose()
    $icon = Join-Path $env:TEMP 'mkw-cover-icon.png'
    $dst.Save($icon, [Drawing.Imaging.ImageFormat]::Png); $dst.Dispose()
    Write-Host "Using the official cover (${w}x${h})."
} catch { Write-Host "Couldn't download the cover; using the generic icon." }

# 5. Copy the files (the console has no scp, so stream them through ssh).
Write-Host "`nCopying files to the console..."
cmd /c "$ssh root@$Console `"rm -rf /tmp/inst; mkdir -p /tmp/inst`"" | Out-Null
$repo = Resolve-Path (Join-Path $here '..\..\..') -ErrorAction SilentlyContinue
$fromRepo = @{
    'mkw-nesmini' = "$repo\build\mkw-nesmini.stripped"; 'padbridge' = "$repo\build\padbridge"
    'mkw.sh' = "$repo\port\nesmini\mkw.sh"; 'S95mkwpc' = "$repo\port\nesmini\S95mkwpc"; 'install.sh' = "$repo\port\nesmini\install.sh"
}
foreach ($f in 'mkw-nesmini', 'padbridge', 'mkw.sh', 'S95mkwpc', 'install.sh', 'icon.png') {
    $p = if ($f -eq 'icon.png') { $icon } else { Join-Path $here "files\$f" }
    if (-not (Test-Path $p) -and $fromRepo[$f]) { $p = $fromRepo[$f] }
    if (-not (Test-Path $p)) {
        if ($f -eq 'mkw-nesmini') { throw "mkw-nesmini not found. Run 'setup.bat build' first." }
        continue
    }
    cmd /c "$ssh root@$Console `"cat > /tmp/inst/$f`" < `"$p`""
    if ($LASTEXITCODE) { throw "Copying $f failed." }
}

# 5. Run the installer on the console.
Write-Host "Installing..."
$cmd = "cd /tmp/inst && sed -i 's/\r`$//' mkw.sh S95mkwpc install.sh && PCUSER='$PcUser' PCPASS='$PcPass' PC='$pcIp' PCSHARE='$Share' sh install.sh && rm -rf /tmp/inst && sync"
cmd /c "$ssh root@$Console `"$cmd`""
if ($LASTEXITCODE) { throw "Install on the console failed." }

Write-Host "`nDone! Restart the console and 'Mario Kart Wii' will be in the menu."
Write-Host "Keep this PC on and connected while playing: the game files come from its '$Share' share."
