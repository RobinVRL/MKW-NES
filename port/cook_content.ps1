# Makes the game files from your own Mario Kart Wii (USA) disc with the
# upstream builder (same steps as src3ds\Build MKW CIA.bat, but non-interactive).
#
#   cook_content.ps1 [-Disc <file.wbfs|.iso>] [-Builder <folder>]
#
# The upstream builder refuses synced folders such as OneDrive, so src3ds is
# copied to a plain folder first (default C:\MKW-3DS-Builder, or $env:MKW_BUILDER).
# Output: <Builder>\build\release\sd-card\3ds\mkw-3ds   Log: <Builder>\build-cia-log.txt
param(
    [string]$Disc,
    [string]$Builder = $(if ($env:MKW_BUILDER) { $env:MKW_BUILDER } else { 'C:\MKW-3DS-Builder' })
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Fail($msg) { Write-Host "`n$msg" -ForegroundColor Red; exit 1 }

# 1. Disc image: argument, else the first .wbfs/.iso in rom\.
if (-not $Disc) {
    $Disc = (Get-ChildItem "$root\rom\*.wbfs", "$root\rom\*.iso" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $Disc -or -not (Test-Path $Disc)) {
    Fail "No disc image found. Put your Mario Kart Wii (USA) .wbfs or .iso in $root\rom\ (or pass -Disc)."
}
$Disc = (Resolve-Path $Disc).Path
Write-Host "Disc:    $Disc"

# 2. Tools the builder needs.
foreach ($t in @(@('git', 'https://git-scm.com/download/win'), @('node', 'https://nodejs.org (LTS)'))) {
    if (-not (Get-Command $t[0] -ErrorAction SilentlyContinue)) { Fail "$($t[0]) is needed: winget install $(if ($t[0] -eq 'git') {'Git.Git'} else {'OpenJS.NodeJS.LTS'})  ($($t[1]))" }
}
if (-not $env:DEVKITPRO -and -not (Test-Path 'C:\devkitPro')) {
    Fail "devkitPro (3DS tools) is needed: https://devkitpro.org/wiki/Getting_Started"
}

# Python 3.11-3.14 (3.15 isn't supported by the builder yet).
$py = $null
$check = 'import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))'
$cands = @()
if (Get-Command py -ErrorAction SilentlyContinue) { $cands += '3.14', '3.13', '3.12', '3.11' | ForEach-Object { , @('py', "-$_") } }
if (Get-Command python -ErrorAction SilentlyContinue) { $cands += , @('python') }
foreach ($c in $cands) {
    $exe = $c[0]; $a = @($c | Select-Object -Skip 1)
    & $exe @a -c $check 2>$null
    if ($LASTEXITCODE -eq 0) { $py = $c; break }
}
if (-not $py) { Fail "Python 3.11-3.14 is needed: winget install Python.Python.3.13" }

# 3. Copy src3ds to a non-synced folder (only new/changed files; existing
#    build output, .venv and vendor checkouts there are kept).
Write-Host "Builder: $Builder"
New-Item -ItemType Directory -Force $Builder | Out-Null
robocopy "$root\src3ds" $Builder /E /XO /NFL /NDL /NJH /NJS /NP /XD .venv build mkw-rom node_modules | Out-Null
if ($LASTEXITCODE -ge 8) { Fail "Copying src3ds to $Builder failed." }
Set-Location $Builder
if (Test-Path ".toolchain\llvm-mingw\bin\clang++.exe") { $env:PATH = "$PWD\.toolchain\llvm-mingw\bin;$env:PATH" }

# 4. Python environment.
$pyExe = $py[0]; $pyArgs = @($py | Select-Object -Skip 1)
if (-not (Test-Path vendor\gclib)) { & $pyExe @pyArgs tools\bootstrap.py; if ($LASTEXITCODE) { Fail "bootstrap.py failed." } }
if (-not (Test-Path .venv\Scripts\python.exe)) { & $pyExe @pyArgs -m venv .venv; if ($LASTEXITCODE) { Fail "Creating .venv failed." } }
$venv = "$PWD\.venv\Scripts\python.exe"
& $venv -c $check 2>$null
if ($LASTEXITCODE) { Fail "$Builder\.venv uses an unsupported Python. Delete that folder and run again." }
& $venv -c "import numpy, PIL, Crypto, unicorn" 2>$null
if ($LASTEXITCODE) { & $venv -m pip install -r requirements.txt; if ($LASTEXITCODE) { Fail "pip install failed." } }

# 5. Build (resumable: finished steps are skipped on the next run).
Write-Host "`nBuilding the game files. The first run takes a few hours...`n"
$env:PYTHONUTF8 = '1'; $env:PYTHONUNBUFFERED = '1'
$ErrorActionPreference = 'Continue' # the builder writes progress to stderr
Set-Content build-cia-log.txt $null -Encoding UTF8
& $venv tools\release\build_cia.py $Disc 2>&1 | ForEach-Object { $l = $_.ToString(); $l; Add-Content build-cia-log.txt $l -Encoding UTF8 }
if ($LASTEXITCODE) { Fail "Build failed. Run again to resume; if it keeps failing, see $Builder\build-cia-log.txt" }
Write-Host "`nGame files: $Builder\build\release\sd-card\3ds\mkw-3ds" -ForegroundColor Green
