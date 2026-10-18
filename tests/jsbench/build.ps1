param([ValidateSet('quickjs','mujs','all')][string]$Engine = 'all')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$devkit = 'C:\devkitPro'
$bin = Join-Path $devkit 'devkitARM\bin'
$tools = Join-Path $devkit 'tools\bin'
$libctru = Join-Path $devkit 'libctru'
$cc = Join-Path $bin 'arm-none-eabi-gcc.exe'
$cxx = Join-Path $bin 'arm-none-eabi-g++.exe'
foreach ($tool in @($cc,$cxx,(Join-Path $tools '3dsxtool.exe'),(Join-Path $tools 'smdhtool.exe'))) { if (!(Test-Path $tool)) { throw "Outil absent: $tool" } }
$arch = @('-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft')
$inc = @('-Ithird_party/js_eval/quickjs','-Ithird_party/js_eval/mujs','-IC:/devkitPro/libctru/include','-IC:/devkitPro/portlibs/3ds/include')
$buildDir = Join-Path $PSScriptRoot 'build'
$engineDir = Join-Path $buildDir 'engines'
New-Item -ItemType Directory -Force $engineDir | Out-Null
$src = Join-Path $PSScriptRoot 'main.c'
$icon = Join-Path $root 'assets\icon.png'

function Build-Engines {
    $flags = @('-O2','-ffunction-sections','-fdata-sections','-D__3DS__') + $arch + @('-Ithird_party/js_eval/quickjs','-Ithird_party/js_eval/mujs')
    $quickFlags = $flags + @('-DCONFIG_VERSION=\"v0.4-bench\"')
    foreach ($file in @('quickjs.c','dtoa.c','libregexp.c','libunicode.c','cutils.c')) {
        $source = Join-Path $root (Join-Path 'third_party\js_eval\quickjs' $file)
        $object = Join-Path $engineDir ($file -replace '\.c$','.o')
        & $cc @quickFlags '-Wno-incompatible-pointer-types' '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "Compilation QuickJS: $file" }
    }
    $muSource = Join-Path $root 'third_party\js_eval\mujs\one.c'
    $muObject = Join-Path $engineDir 'mujs_one.o'
    & $cc @flags '-c' $muSource '-o' $muObject
    if ($LASTEXITCODE -ne 0) { throw 'Compilation MuJS echouee' }
}

function Build-Bench([string]$name,[string[]]$defines,[string[]]$engineObjects) {
    $obj = Join-Path $buildDir ($name+'.o')
    $elf = Join-Path $buildDir ($name+'.elf')
    $smdh = Join-Path $buildDir ($name+'.smdh')
    $out3dsx = Join-Path $buildDir ($name+'.3dsx')
    & $cc '-O2' '-Wall' '-ffunction-sections' '-D__3DS__' @arch @inc @defines '-c' $src '-o' $obj
    if ($LASTEXITCODE -ne 0) { throw "Compilation $name echouee" }
    $ctruArchive = Join-Path $libctru 'lib\libctru.a'
    & $cxx '-specs=3dsx.specs' @arch '-Wl,--gc-sections' $obj @engineObjects $ctruArchive '-lm' '-o' $elf
    if ($LASTEXITCODE -ne 0) { throw "Edition de liens $name echouee" }
    $title = if ($name -eq 'quickjs') { 'JSBench QuickJS' } else { 'JSBench MuJS' }
    & (Join-Path $tools 'smdhtool.exe') '--create' $title 'Mesure JavaScript New 3DS' 'Pixelnaute' $icon $smdh
    if ($LASTEXITCODE -ne 0) { throw "SMDH $name echoue" }
    & (Join-Path $tools '3dsxtool.exe') $elf $out3dsx "--smdh=$smdh"
    if ($LASTEXITCODE -ne 0) { throw "3DSX $name echoue" }
    $bytes = (Get-Item $out3dsx).Length
    Write-Host "$name : $out3dsx ($bytes octets)"
}

Push-Location $root
try {
    Build-Engines
    $quickObjects = @('quickjs.o','dtoa.o','libregexp.o','libunicode.o','cutils.o') | ForEach-Object { Join-Path $engineDir $_ }
    $muObjects = @(Join-Path $engineDir 'mujs_one.o')
    if ($Engine -in @('quickjs','all')) { Build-Bench 'quickjs' @('-DBENCH_QUICKJS') $quickObjects }
    if ($Engine -in @('mujs','all')) { Build-Bench 'mujs' @() $muObjects }
}
finally { Pop-Location }

