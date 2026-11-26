# One-stop setup for Windows. Run through setup.bat (double-click), or:
#
#   setup.bat                 do every step that isn't done yet
#   setup.bat check           show what's ready and what's missing
#   setup.bat game-files      make the game files from your disc (rom\*.wbfs|iso)
#   setup.bat build           build the console program (uses WSL)
#   setup.bat share           share the game files with the console (admin)
#   setup.bat install         add "Mario Kart Wii" to the console's menu
#   setup.bat package         make dist\MKW-NES-Menu-Installer.zip
param([string]$Step = 'all', [string]$Console = '169.254.1.1')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$builder = if ($env:MKW_BUILDER) { $env:MKW_BUILDER } else { 'C:\MKW-3DS-Builder' }
$sdCard = Join-Path $builder 'build\release\sd-card'
$bin = Join-Path $root 'build\mkw-nesmini.stripped'
$ssh = 'ssh -o ConnectTimeout=4 -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -o BatchMode=yes -o LogLevel=ERROR'

function Say($m) { Write-Host "`n== $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "`n$m" -ForegroundColor Red; exit 1 }
function Run($script, [string[]]$a = @()) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script @a
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
function HaveGameFiles { Test-Path (Join-Path $sdCard '3ds\mkw-3ds\race.pak') }
function ConsoleUp { cmd /c "$ssh root@$Console true 2>NUL"; return ($LASTEXITCODE -eq 0) }

function Step-Check {
    Say 'Status'
    $rows = @(
        @((Get-ChildItem "$root\rom\*.wbfs", "$root\rom\*.iso" -ErrorAction SilentlyContinue), 'disc image in rom\', 'copy your own Mario Kart Wii (USA) .wbfs/.iso into rom\'),
        @((HaveGameFiles), "game files ($sdCard)", 'setup.bat game-files'),
        @((Get-Command wsl -ErrorAction SilentlyContinue), 'WSL', 'wsl --install -d Ubuntu  (then restart)'),
        @((Test-Path "$root\build\sysroot\libc.so.6"), 'build\sysroot (console libraries)', 'plug in the console, then setup.bat build'),
        @((Test-Path $bin), 'console program', 'setup.bat build'),
        @((Get-SmbShare -Name MKW -ErrorAction SilentlyContinue), 'PC share "MKW"', 'setup.bat share'),
        @((Get-Command ssh -ErrorAction SilentlyContinue), 'OpenSSH client', 'Settings > Apps > Optional features > OpenSSH Client')
    )
    foreach ($r in $rows) {
        if ($r[0]) { Write-Host "  [ok]      $($r[1])" -ForegroundColor Green }
        else { Write-Host "  [missing] $($r[1])  ->  $($r[2])" -ForegroundColor Yellow }
    }
}

function Step-GameFiles {
    Say 'Making the game files from your disc'
    Run "$root\port\cook_content.ps1" @('-Builder', $builder)
}

function Step-Sysroot {
    $sys = Join-Path $root 'build\sysroot'
    if (Test-Path "$sys\libc.so.6") { return }
    if (-not (ConsoleUp)) {
        Write-Host "Console not reachable at $Console, so build\sysroot can't be fetched." -ForegroundColor Yellow
        Write-Host "The build will still work but may not load on the console. Plug it in and run 'setup.bat build' again."
        return
    }
    Say "Copying the console's libraries to build\sysroot"
    New-Item -ItemType Directory -Force $sys | Out-Null
    foreach ($f in 'libc.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1', 'libm.so.6', 'ld-linux-armhf.so.3', 'libMali.so') {
        $remote = "for d in /lib /usr/lib; do [ -f `$d/$f ] && exec cat `$d/$f; done; exit 1"
        cmd /c "$ssh root@$Console `"$remote`" > `"$sys\$f`""
        if ($LASTEXITCODE) { Remove-Item "$sys\$f"; Write-Host "  $f (not found, skipped)" } else { Write-Host "  $f" }
    }
}

function Step-Build {
    if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
        Fail "WSL is needed to build. Run 'wsl --install -d Ubuntu' in an admin PowerShell, restart, then run this again."
    }
    # WSL can't reach hakchi's link-local address, so fetch the libraries from Windows.
    Step-Sysroot
    Say 'Building the console program in WSL (installs the cross compiler on first run)'
    $wslRoot = (wsl wslpath -a ($root -replace '\\', '/')).Trim()
    wsl bash -c "cd '$wslRoot' && bash setup.sh deps && bash setup.sh build"
    if ($LASTEXITCODE) { Fail 'Build failed (see the messages above).' }
}

function Step-Share {
    if (-not (HaveGameFiles)) { Fail "No game files at $sdCard yet. Run 'setup.bat game-files' first." }
    Say 'Sharing the game files with the console'
    Run "$root\port\nesmini\share.ps1" @('-Path', $sdCard, '-Name', 'MKW')
}

function Step-Install {
    if (-not (Test-Path $bin)) { Fail "No console program yet. Run 'setup.bat build' first." }
    Say 'Installing the menu entry on the console'
    Run "$root\port\nesmini\easy-install\Install.ps1" @('-Console', $Console, '-Share', 'MKW')
}

function Step-Package {
    if (-not (Test-Path $bin)) { Fail "No console program yet. Run 'setup.bat build' first." }
    Say 'Packaging dist\MKW-NES-Menu-Installer.zip'
    $tmp = Join-Path $env:TEMP 'mkw-pkg'
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$tmp\files" | Out-Null
    $ei = "$root\port\nesmini\easy-install"
    Copy-Item "$ei\Install.bat", "$ei\Install.ps1", "$ei\README.txt" $tmp
    Copy-Item "$root\port\nesmini\share.ps1" "$tmp\Setup-PC-Share.ps1"
    Copy-Item $bin "$tmp\files\mkw-nesmini"
    Copy-Item "$root\port\nesmini\mkw.sh", "$root\port\nesmini\S95mkwpc", "$root\port\nesmini\install.sh" "$tmp\files"
    if (Test-Path "$root\build\padbridge") { Copy-Item "$root\build\padbridge" "$tmp\files" }
    if (Test-Path "$ei\files\icon.png") { Copy-Item "$ei\files\icon.png" "$tmp\files" }
    New-Item -ItemType Directory -Force "$root\dist" | Out-Null
    Compress-Archive -Path "$tmp\*" -DestinationPath "$root\dist\MKW-NES-Menu-Installer.zip" -Force
    Remove-Item -Recurse -Force $tmp
    Write-Host "Saved dist\MKW-NES-Menu-Installer.zip"
}

switch ($Step) {
    'check'      { Step-Check }
    'game-files' { Step-GameFiles }
    'build'      { Step-Build }
    'share'      { Step-Share }
    'install'    { Step-Install }
    'package'    { Step-Package }
    'all' {
        if (HaveGameFiles) { Write-Host "Game files already made ($sdCard)." } else { Step-GameFiles }
        Step-Build
        Step-Share
        if (ConsoleUp) { Step-Install }
        else { Write-Host "`nConsole not found at $Console. Plug it in, turn it on, then run: setup.bat install" -ForegroundColor Yellow }
        Step-Check
    }
    default { Get-Content $PSCommandPath | Select-Object -Skip 1 -First 9; exit 1 }
}
