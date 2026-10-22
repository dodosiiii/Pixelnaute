param(
    [string]$PythonCommand = "python"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$localData = [Environment]::GetFolderPath("LocalApplicationData")
$toolsRoot = Join-Path $localData "Pixelnaute\banner-tools"
$venv = Join-Path $toolsRoot "venv"
$python = Join-Path $venv "Scripts\python.exe"
$converterRoot = Join-Path $toolsRoot "pycgfx-1f78850086f3a77c41e07162e842f97a5bf3c18a"
$converterCommit = "1f78850086f3a77c41e07162e842f97a5bf3c18a"
$glb = Join-Path $env:TEMP "pixelnaute-banner.glb"
$cgfx = Join-Path $root "assets\pixelnaute-banner.cgfx"
$preview = Join-Path $root "assets\pixelnaute-banner-preview.svg"

if (-not (Test-Path $python)) {
    $pythonExe = (Get-Command $PythonCommand -ErrorAction Stop).Source
    New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
    & $pythonExe -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw "Could not create the banner Python environment." }
}

& $python -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot "requirements.txt")
if ($LASTEXITCODE -ne 0) { throw "Could not install banner generator requirements." }

if (-not (Test-Path (Join-Path $converterRoot "main.py"))) {
    $git = (Get-Command git -ErrorAction Stop).Source
    & $git clone --no-checkout https://github.com/skyfloogle/pycgfx.git $converterRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not clone the pinned pycgfx converter." }
    & $git -C $converterRoot checkout $converterCommit
    if ($LASTEXITCODE -ne 0) { throw "Could not select the pinned pycgfx revision." }
}

$actualCommit = (& git -C $converterRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $converterCommit) {
    throw "The local pycgfx checkout is not at the expected revision $converterCommit."
}

& $python (Join-Path $PSScriptRoot "create_banner_gltf.py") $glb $preview
if ($LASTEXITCODE -ne 0) { throw "Could not create the animated banner model." }
& $python (Join-Path $PSScriptRoot "convert_banner_to_cgfx.py") $converterRoot $glb $cgfx
if ($LASTEXITCODE -ne 0) { throw "Could not convert the animated banner to CGFX." }

$cgfxBytes = [IO.File]::ReadAllBytes($cgfx)
if ($cgfxBytes.Length -lt 4 -or
    [Text.Encoding]::ASCII.GetString($cgfxBytes, 0, 4) -ne "CGFX") {
    throw "The generated banner does not have a valid CGFX header."
}
if ($cgfxBytes.Length -gt 512KB) {
    throw "The generated CGFX banner exceeds the 3DS 512 KiB limit."
}
Write-Host "OK -> assets\pixelnaute-banner.cgfx ($($cgfxBytes.Length) bytes)" -ForegroundColor Green
