# Verification hors console du decodeur de police
#
# Compile et execute tests/pc, qui valide le decodage des feuilles de glyphes
# de la police systeme 3DS (format A4 + entrelacement 8x8) sans avoir besoin
# d'une console ni d'un emulateur, puis produit une planche de controle PNG.
#
# Usage :
#   powershell -ExecutionPolicy Bypass -File tests\pc\run.ps1
#
# Prerequis : Visual Studio (cl.exe) et Pillow (voir tools/pc_render_to_png.py).
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # N3DSWeb/
$workspace = Split-Path -Parent $root                          # dossier du projet
$pcDir = $PSScriptRoot
$venvPython = Join-Path $workspace ".venv\Scripts\python.exe"

# --- localise vcvars64.bat ---
$vcvars = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse `
    -Filter "vcvars64.bat" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $vcvars) {
    Write-Host "Visual Studio (vcvars64.bat) introuvable." -ForegroundColor Red
    exit 1
}

Write-Host "Compilation du test PC..." -ForegroundColor Cyan
$sources = "main_pc.cpp ..\..\source\render\text.cpp ..\..\source\render\bitmap.cpp " +
           "..\..\source\render\stb_truetype_impl.c " +
           "..\..\source\css\css.cpp ..\..\source\html\dom.cpp"
$cmd = "`"$vcvars`" >nul 2>&1 && cd /d `"$pcDir`" && " +
       "cl /nologo /std:c++17 /EHsc /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /I. " +
       "$sources /Fe:pc_test.exe"
cmd.exe /c $cmd 2>&1 | Where-Object { $_ -match "error" }
if ($LASTEXITCODE -ne 0) {
    Write-Host "Echec de compilation du test." -ForegroundColor Red
    exit 1
}

Write-Host ""
& (Join-Path $pcDir "pc_test.exe")
$testExit = $LASTEXITCODE

# --- convertit la planche en PNG ---
if (Test-Path (Join-Path $pcDir "text_sheet.rgb")) {
    $python = if (Test-Path $venvPython) { $venvPython } else { "python" }
    $tool = Join-Path $workspace "tools\pc_render_to_png.py"
    & $python $tool (Join-Path $pcDir "text_sheet.rgb") `
        (Join-Path $pcDir "text_sheet.png") 400 300 2
}

exit $testExit
