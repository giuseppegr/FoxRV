@echo off
rem Builds FOXRV for the Nintendo 3DS on Windows: build-3ds\FoxRV.3dsx.
rem
rem Runs build-3ds.sh in devkitPro's MSYS2 shell (devkitPro's toolchain needs it). Needs
rem devkitPro with "3DS Development" (https://devkitpro.org/wiki/Getting_Started), installed
rem in C:\devkitPro, or DEVKITPRO_MSYS2 set to its msys2 folder.
rem
rem Extra arguments go to CMake's configure step.
setlocal
cd /d "%~dp0"

if not defined DEVKITPRO_MSYS2 set "DEVKITPRO_MSYS2=C:\devkitPro\msys2"
if not exist "%DEVKITPRO_MSYS2%\usr\bin\bash.exe" (
    echo build-3ds.cmd: devkitPro's MSYS2 not found in %DEVKITPRO_MSYS2%. See the top of this file.
    goto :fail
)

rem A login shell sets DEVKITPRO; CHERE_INVOKING keeps it in this folder.
set "MSYSTEM=MSYS"
set "CHERE_INVOKING=1"
"%DEVKITPRO_MSYS2%\usr\bin\bash.exe" -lc "./build-3ds.sh \"$@\"" build-3ds.sh %* || goto :fail
exit /b 0

:fail
echo.
echo Build failed.
rem Keep the window open when started from Explorer.
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b 1
