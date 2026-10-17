@echo off
rem Builds FOXRV in release mode on Windows: build\FoxRV.exe, with engine.ini next to it.
rem
rem Needs MinGW-w64 GCC from MSYS2 (https://www.msys2.org), in its UCRT64 shell:
rem     pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
rem and git (Git for Windows), which CMake uses to download SDL3. C:\msys64 is found by
rem itself; another MSYS2 folder has to be on PATH.
rem
rem Extra arguments go to CMake's configure step, e.g.
rem     build.cmd -DFOXRV_GAME_SOURCE=C:\Games\Fox
rem
rem It also packs a release zip, build\FoxRV-<version>-windows-<arch>.zip, for GitHub's
rem Releases: a FoxRV folder with FoxRV.exe, engine.ini and an empty game folder. The game's
rem own files in build\game are never packed.
setlocal
cd /d "%~dp0"

where gcc >nul 2>nul || call :find_msys2
where gcc >nul 2>nul || (echo build.cmd: gcc not found. Install MSYS2's MinGW-w64 GCC, see the top of this file. & goto :fail)
where cmake >nul 2>nul || (echo build.cmd: cmake not found. & goto :fail)
where git >nul 2>nul || (echo build.cmd: git not found. & goto :fail)

rem A new build folder: Ninja if there is one, else MinGW's make. An existing one keeps its generator.
set "GENERATOR="
if not exist build\CMakeCache.txt (
    where ninja >nul 2>nul && (set "GENERATOR=-G Ninja") || (set "GENERATOR=-G "MinGW Makefiles"")
)

cmake -S . -B build %GENERATOR% -DCMAKE_BUILD_TYPE=Release %* || goto :fail
cmake --build build --parallel %NUMBER_OF_PROCESSORS% || goto :fail

rem The release zip, assembled in its own folder so build\game (the game's files) stays out.
for /f "tokens=2 delims==" %%v in ('findstr /b "CMAKE_PROJECT_VERSION:STATIC=" build\CMakeCache.txt') do set "VERSION=%%v"
for /f "tokens=1 delims=-" %%a in ('gcc -dumpmachine') do set "ARCH=%%a"
set "ZIP_NAME=FoxRV-%VERSION%-windows-%ARCH%.zip"
if exist build\release rmdir /s /q build\release
if exist "build\%ZIP_NAME%" del "build\%ZIP_NAME%"
mkdir build\release\FoxRV\game || goto :fail
copy /y build\FoxRV.exe build\release\FoxRV\ >nul || goto :fail
copy /y engine.ini build\release\FoxRV\ >nul || goto :fail
where strip >nul 2>nul && strip build\release\FoxRV\FoxRV.exe
pushd build\release
cmake -E tar cf "..\%ZIP_NAME%" --format=zip FoxRV || (popd & goto :fail)
popd

echo.
echo Built build\FoxRV.exe. Put the original game's files in build\game (see README.md).
echo Release: build\%ZIP_NAME%
exit /b 0

:find_msys2
for %%d in (C:\msys64\ucrt64\bin C:\msys64\mingw64\bin C:\msys64\clang64\bin) do (
    if exist "%%d\gcc.exe" (
        set "PATH=%%d;%PATH%"
        exit /b 0
    )
)
exit /b 1

:fail
echo.
echo Build failed.
rem Keep the window open when started from Explorer.
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b 1
