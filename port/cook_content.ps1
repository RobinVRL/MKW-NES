# Make the game files from the user's own USA disc with upstream's builder
# (same steps as src3ds/Build MKW CIA.bat, non-interactive). Log: src3ds/build-cia-log.txt
param([string]$Disc = (Get-ChildItem "$PSScriptRoot\..\rom\*.wbfs","$PSScriptRoot\..\rom\*.iso" | Select -First 1).FullName)
$ErrorActionPreference = 'Continue' # native tools write progress to stderr
# upstream refuses synced folders (OneDrive): run from a plain copy of src3ds
$builder = if ($env:MKW_BUILDER) { $env:MKW_BUILDER } else { 'C:\MKW-3DS-Builder' }
Set-Location $builder
if (Test-Path ".toolchain\llvm-mingw\bin\clang++.exe") { $env:PATH = "$PWD\.toolchain\llvm-mingw\bin;$env:PATH" }
if (-not (Test-Path vendor\gclib)) { py -3.14 tools\bootstrap.py; if ($LASTEXITCODE) { exit 1 } }
if (-not (Test-Path .venv\Scripts\python.exe)) { py -3.14 -m venv .venv }
& .venv\Scripts\python.exe -c "import numpy, PIL, Crypto, unicorn" 2>$null
if ($LASTEXITCODE) { & .venv\Scripts\python.exe -m pip install -r requirements.txt; if ($LASTEXITCODE) { exit 1 } }
$env:PYTHONUTF8 = '1'; $env:PYTHONUNBUFFERED = '1'
& .venv\Scripts\python.exe tools\release\build_cia.py $Disc 2>&1 | ForEach-Object { $l = $_.ToString(); $l; Add-Content build-cia-log.txt $l -Encoding UTF8 }
exit $LASTEXITCODE
