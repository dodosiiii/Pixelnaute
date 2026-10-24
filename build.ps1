# Usage:
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -BuildCIA
param([switch]$BuildCIA)

# Compilation de N3DS Web
#
# Le projet contient des espaces dans son chemin et le make MSYS2 n'est pas
# toujours utilisable : ce script appelle directement la chaine devkitARM.
#
# Usage :
#   powershell -ExecutionPolicy Bypass -File build.ps1
#
# Resultat : N3DSWeb.3dsx a la racine du projet; -BuildCIA ajoute le .cia.
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
$bannerTool = Join-Path $toolbin "bannertool.exe"
$smdhTool = Join-Path $toolbin "smdhtool.exe"
$makerom = Join-Path $toolbin "makerom.exe"

$requiredTools = @($gxx, (Join-Path $toolbin "3dsxtool.exe"), $smdhTool)
if ($BuildCIA) { $requiredTools += @($bannerTool, $makerom) }
foreach ($tool in $requiredTools) {
    if (-not (Test-Path $tool)) { throw "Outil introuvable : $tool (devkitPro est-il installe ?)" }
}

$quickJsPatch = Join-Path $root "tools\quickjs-newlib-3ds.patch"
& git -C $quickjsDir apply --reverse --check $quickJsPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    & git -C $quickjsDir apply --check $quickJsPatch
    if ($LASTEXITCODE -ne 0) { throw "Le correctif Newlib QuickJS ne s'applique pas." }
    & git -C $quickjsDir apply $quickJsPatch
    if ($LASTEXITCODE -ne 0) { throw "Echec d'application du correctif Newlib QuickJS." }
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
$smdh = "$target.smdh"
if ((Test-Path $icon) -and
    (-not (Test-Path $smdh) -or
     (Get-Item $icon).LastWriteTimeUtc -gt (Get-Item $smdh).LastWriteTimeUtc)) {
    & $smdhTool --create "N3DS Web" `
        "Navigateur web autonome pour New 3DS" "dodosiiii" $icon $smdh
    if ($LASTEXITCODE -ne 0) { throw "Echec de creation de l'icone SMDH." }
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

if ($BuildCIA) {
    $bannerImage = Join-Path $root "assets\cia_banner.png"
    $bannerCgfx = Join-Path $root "assets\pixelnaute-banner.cgfx"
    $bannerAudio = Join-Path $root "assets\cia_banner.wav"
    $icon = Join-Path $root "assets\icon.png"
    $rsf = Join-Path $root "cia\N3DSWeb.rsf"
    foreach ($input in @($bannerAudio, $icon, $rsf)) {
        if (-not (Test-Path $input)) { throw "Fichier CIA introuvable : $input" }
    }
    if (-not (Test-Path $bannerCgfx) -and -not (Test-Path $bannerImage)) {
        throw "Banniere CIA introuvable : fournir assets\pixelnaute-banner.cgfx ou assets\cia_banner.png."
    }
    if (Test-Path $bannerCgfx) {
        $cgfxBytes = [IO.File]::ReadAllBytes($bannerCgfx)
        if ($cgfxBytes.Length -lt 4 -or
            [Text.Encoding]::ASCII.GetString($cgfxBytes, 0, 4) -ne "CGFX" -or
            $cgfxBytes.Length -gt 512KB) {
            throw "La banniere CGFX doit etre valide et ne pas depasser 512 KiB."
        }
    }

    $audioBytes = [IO.File]::ReadAllBytes($bannerAudio)
    if ($audioBytes.Length -lt 44 -or
        [Text.Encoding]::ASCII.GetString($audioBytes, 0, 4) -ne "RIFF" -or
        [Text.Encoding]::ASCII.GetString($audioBytes, 8, 4) -ne "WAVE") {
        throw "Le son de bannière CIA n'est pas un fichier WAV RIFF valide."
    }
    $audioOffset = 12
    $audioFormat = 0
    $audioChannels = 0
    $audioSampleRate = 0
    $audioBits = 0
    $audioBlockAlign = 0
    $audioDataBytes = 0
    while ($audioOffset + 8 -le $audioBytes.Length) {
        $chunkName = [Text.Encoding]::ASCII.GetString($audioBytes, $audioOffset, 4)
        $chunkSize = [BitConverter]::ToUInt32($audioBytes, $audioOffset + 4)
        $chunkData = $audioOffset + 8
        if ($chunkSize -gt $audioBytes.Length - $chunkData) {
            throw "Le WAV de bannière CIA contient un bloc tronqué."
        }
        if ($chunkName -eq "fmt " -and $chunkSize -ge 16) {
            $audioFormat = [BitConverter]::ToUInt16($audioBytes, $chunkData)
            $audioChannels = [BitConverter]::ToUInt16($audioBytes, $chunkData + 2)
            $audioSampleRate = [BitConverter]::ToUInt32($audioBytes, $chunkData + 4)
            $audioBlockAlign = [BitConverter]::ToUInt16($audioBytes, $chunkData + 12)
            $audioBits = [BitConverter]::ToUInt16($audioBytes, $chunkData + 14)
        } elseif ($chunkName -eq "data") {
            $audioDataBytes += $chunkSize
        }
        $audioOffset = $chunkData + $chunkSize + ($chunkSize % 2)
    }
    if ($audioFormat -ne 1 -or $audioChannels -ne 2 -or $audioSampleRate -le 0 -or
        $audioBlockAlign -le 0 -or $audioBits -ne 16 -or $audioDataBytes -le 0) {
        throw "Le son de bannière doit être du PCM 16 bits stéréo valide."
    }
    $audioDuration = $audioDataBytes / $audioBlockAlign / $audioSampleRate
    if ($audioDuration -gt 3) {
        throw "Le son de bannière dure $([math]::Round($audioDuration, 3)) s; la limite 3DS est de 3 s."
    }
    Write-Host "WAV CIA valide ($([math]::Round($audioDuration, 3)) s, stéréo)" -ForegroundColor DarkCyan

    $banner = Join-Path $buildDir "N3DSWeb.bnr"
    Write-Host "BNR N3DSWeb.bnr" -ForegroundColor Cyan
    if (Test-Path $bannerCgfx) {
        Write-Host "Banniere HOME animee (CGFX)" -ForegroundColor DarkCyan
        & $bannerTool makebanner -ci $bannerCgfx -a $bannerAudio -o $banner
    } else {
        & $bannerTool makebanner -i $bannerImage -a $bannerAudio -o $banner
    }
    if ($LASTEXITCODE -ne 0) { throw "Echec de creation de la banniere CIA." }

    $ciaIcon = Join-Path $buildDir "N3DSWeb-cia.smdh"
    & $bannerTool makesmdh -s "Pixelnaute" -l "Navigateur web autonome pour New 3DS" `
        -p "dodosiiii" -i $icon -o $ciaIcon -f "visible,new3ds"
    if ($LASTEXITCODE -ne 0) { throw "Echec de creation de l'icone CIA." }
    $iconBytes = [IO.File]::ReadAllBytes($ciaIcon)
    $iconFlags = [BitConverter]::ToUInt32($iconBytes, 0x2028)
    if (($iconFlags -band 0x01) -eq 0) { throw "L'icone CIA n'est pas visible dans le menu HOME." }

    $version = (Select-String -Path (Join-Path $root "source\common.hpp") `
        -Pattern '^#define NW_VERSION_STRING "([^"]+)"$').Matches.Groups[1].Value
    if (-not $version) { throw "Version de l'application introuvable dans source\common.hpp." }
    $versionParts = $version.Split(".")
    if ($versionParts.Count -ne 3) { throw "Version CIA invalide : $version" }

    $makeromArgs = @(
        "-f", "cia", "-target", "t", "-rsf", $rsf, "-elf", "$target.elf",
        "-icon", $ciaIcon, "-banner", $banner,
        "-major", $versionParts[0], "-minor", $versionParts[1], "-micro", $versionParts[2],
        "-DAPP_ROMFS=$romfsDir", "-o", "$target.cia"
    )
    Write-Host "CIA N3DSWeb.cia" -ForegroundColor Cyan
    & $makerom @makeromArgs
    if ($LASTEXITCODE -ne 0) { throw "Echec de creation du CIA." }
    $ciaSize = (Get-Item "$target.cia").Length
    Write-Host "OK -> N3DSWeb.cia ($([math]::Round($ciaSize / 1KB)) Ko)" -ForegroundColor Green
}
