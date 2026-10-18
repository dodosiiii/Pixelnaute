# Compilation de N3DS Web
#
# Le projet contient des espaces dans son chemin et le make MSYS2 n'est pas
# toujours utilisable : ce script appelle directement la chaine devkitARM.
#
# Usage :
#   powershell -ExecutionPolicy Bypass -File build.ps1
#
# Resultat : N3DSWeb.3dsx a la racine du projet.
#
# Remarque : on ne met pas $ErrorActionPreference a "Stop". Sous PowerShell 5.1,
# un compilateur qui ecrit un simple avertissement sur stderr serait alors
# traite comme une erreur fatale. Les codes de retour sont testes explicitement.
$root = $PSScriptRoot

$env:DEVKITPRO = "C:\devkitPro"
$env:DEVKITARM = "C:\devkitPro\devkitARM"
$bin      = Join-Path $env:DEVKITARM "bin"
$gxx      = Join-Path $bin "arm-none-eabi-g++.exe"
$toolbin  = Join-Path $env:DEVKITPRO "tools\bin"
$ctrulib  = Join-Path $env:DEVKITPRO "libctru"
$portlibs = Join-Path $env:DEVKITPRO "portlibs\3ds"
$quickjsDir = Join-Path $root "third_party\js_eval\quickjs"
$target   = Join-Path $root "N3DSWeb"
$buildDir = Join-Path $root "build"

foreach ($tool in @($gxx, (Join-Path $toolbin "3dsxtool.exe"))) {
    if (-not (Test-Path $tool)) { throw "Outil introuvable : $tool (devkitPro est-il installe ?)" }
}

$arch = @("-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft")
$cflags   = @("-g", "-Wall", "-O2", "-mword-relocations", "-ffunction-sections") +
            $arch + @("-D__3DS__")
$cxxflags = @("-g", "-Wall", "-O2", "-mword-relocations", "-ffunction-sections") + $arch +
            @("-fno-rtti", "-fno-exceptions", "-std=gnu++17", "-D__3DS__")
$ldflags  = @("-specs=3dsx.specs", "-g") + $arch + @("-Wl,-Map,$target.map")
$libs     = @("-lcitro2d", "-lcitro3d", "-lcurl", "-lmbedtls", "-lmbedx509",
              "-lmbedcrypto", "-lz", "-lctru", "-lm")
$libpaths = @("-L$(Join-Path $portlibs 'lib')", "-L$(Join-Path $ctrulib 'lib')")

# Tous les sous-dossiers de source/ sont ajoutes au chemin d'inclusion.
$sourceDirs = @(Get-ChildItem -Path (Join-Path $root "source") -Directory -Recurse |
                Select-Object -ExpandProperty FullName)
$includes = @($sourceDirs | ForEach-Object { "-I$_" }) +
            @("-I$(Join-Path $root 'source')") +
            @("-I$quickjsDir") +
            @("-I$(Join-Path $portlibs 'include')") +
            @("-I$(Join-Path $ctrulib 'include')")

# Sources C++ et C (stb_image est un fichier C).
$sources = @(Get-ChildItem -Path (Join-Path $root "source") -Include *.cpp, *.c -Recurse |
             Sort-Object FullName)
if ($sources.Count -eq 0) { throw "Aucun fichier source dans source/" }

if (Test-Path $buildDir) { Remove-Item $buildDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$gcc = Join-Path $bin "arm-none-eabi-gcc.exe"
$objects = @()
$errors = @()
$warnings = 0
foreach ($src in $sources) {
    $obj = Join-Path $buildDir ($src.BaseName + ".o")
    $objects += $obj
    if ($src.Extension -eq ".c") {
        Write-Host "CC  $($src.Name)" -ForegroundColor DarkCyan
        $out = & $gcc @cflags @includes -c $src.FullName -o $obj 2>&1
    } else {
        Write-Host "CXX $($src.Name)" -ForegroundColor Cyan
        $out = & $gxx @cxxflags @includes -c $src.FullName -o $obj 2>&1
    }
    if ($LASTEXITCODE -ne 0) { $errors += ($out | Where-Object { $_ -match "error" }) }
    $warnings += @($out | Where-Object { $_ -match "warning" }).Count
}

# QuickJS est une dependance C tierce : compiler uniquement les unites utiles.
$quickJsFiles = @("quickjs.c", "dtoa.c", "libregexp.c", "libunicode.c", "cutils.c")
foreach ($file in $quickJsFiles) {
    $src = Join-Path $quickjsDir $file
    $obj = Join-Path $buildDir ("js_" + [IO.Path]::GetFileNameWithoutExtension($file) + ".o")
    $objects += $obj
    Write-Host "CC  QuickJS $file" -ForegroundColor DarkCyan
    $out = & $gcc @cflags @includes '-Wno-incompatible-pointer-types' '-Wno-format' '-Wno-maybe-uninitialized' '-DCONFIG_VERSION=\"v0.4.0\"' -c $src -o $obj 2>&1
    if ($LASTEXITCODE -ne 0) { $errors += ($out | Where-Object { $_ -match "error" }) }
    $warnings += @($out | Where-Object { $_ -match "warning" }).Count
}

if ($errors.Count -gt 0) {
    Write-Host "ECHEC DE COMPILATION" -ForegroundColor Red
    $errors | Select-Object -First 40 | ForEach-Object { Write-Host $_ }
    exit 1
}

Write-Host "LD  N3DSWeb.elf" -ForegroundColor Cyan
& $gxx @ldflags @objects @libpaths @libs -o "$target.elf"
if ($LASTEXITCODE -ne 0) { throw "Echec de l'edition de liens." }

$icon = Join-Path $root "assets\icon.png"
if (-not (Test-Path "$target.smdh") -and (Test-Path $icon)) {
    & (Join-Path $toolbin "smdhtool.exe") --create "N3DS Web" `
        "Navigateur web autonome pour New 3DS" "dodosiiii" $icon "$target.smdh"
}

$romfsArg = @()
$romfsDir = Join-Path $root "romfs"
if (Test-Path $romfsDir) {
    $romfsArg = @("--romfs=$romfsDir")
}

Write-Host "3DSX N3DSWeb.3dsx" -ForegroundColor Cyan
& (Join-Path $toolbin "3dsxtool.exe") "$target.elf" "$target.3dsx" `
    "--smdh=$target.smdh" @romfsArg
if ($LASTEXITCODE -ne 0) { throw "Echec de creation du .3dsx." }

$size = (Get-Item "$target.3dsx").Length
Write-Host "OK -> N3DSWeb.3dsx ($([math]::Round($size / 1KB)) Ko, $warnings avertissements)" -ForegroundColor Green
