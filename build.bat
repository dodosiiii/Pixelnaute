@echo off
REM =====================================================================
REM  N3DS Web - script de compilation (Windows)
REM
REM  Le chemin du projet contient des espaces ("google for new 3ds"),
REM  ce qui bloque GNU make. Ce script compile dans un dossier temporaire
REM  sans espace puis recopie les resultats ici.
REM  Usage: build.bat [cia] pour produire aussi N3DSWeb.cia.
REM =====================================================================
setlocal
if not defined TEMP set TEMP=C:\Users\%USERNAME%\AppData\Local\Temp
set DST=%TEMP%\N3DSWeb-build
set BASH=C:\devkitPro\msys2\usr\bin\bash.exe
set MAKE_TARGET=
if /I "%~1"=="cia" set MAKE_TARGET=cia

echo Synchronisation vers %DST% ...
if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%" 2>nul
xcopy /E /I /Y /Q "%~dp0source" "%DST%\source"  >nul
xcopy /E /I /Y /Q "%~dp0assets" "%DST%\assets"  >nul
xcopy /E /I /Y /Q "%~dp0romfs" "%DST%\romfs" >nul
xcopy /E /I /Y /Q "%~dp0cia" "%DST%\cia" >nul
xcopy /E /I /Y /Q "%~dp0third_party\js_eval\quickjs" "%DST%\third_party\js_eval\quickjs" >nul
copy /Y "%~dp0Makefile" "%DST%\Makefile" >nul

REM conversion du chemin Windows vers le style msys (/c/Users/...)
set MSYSPATH=%TEMP:\=/%
set MSYSPATH=%MSYSPATH:C:\=/c/%

echo Compilation ...
"%BASH%" -lc "cd '%MSYSPATH%/N3DSWeb-build' && make V=0 %MAKE_TARGET%"
if errorlevel 1 (
  echo.
  echo  BUILD ECHOUE
  exit /b 1
)

copy /Y "%DST%\N3DSWeb.3dsx" "%~dp0N3DSWeb.3dsx" >nul
copy /Y "%DST%\N3DSWeb.smdh" "%~dp0N3DSWeb.smdh" >nul
copy /Y "%DST%\N3DSWeb.elf"  "%~dp0N3DSWeb.elf"  >nul
if /I "%MAKE_TARGET%"=="cia" copy /Y "%DST%\N3DSWeb.cia" "%~dp0N3DSWeb.cia" >nul
echo.
if /I "%MAKE_TARGET%"=="cia" (
  echo  OK : N3DSWeb.3dsx et N3DSWeb.cia sont a jour
) else (
  echo  OK : N3DSWeb.3dsx est a jour
)
endlocal
