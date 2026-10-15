#!/bin/sh
# Builds FOXRV for the Nintendo 3DS: build-3ds/FoxRV.3dsx, for the Homebrew Launcher.
#
# Needs devkitPro with the 3DS tools (https://devkitpro.org/wiki/Getting_Started; on Windows
# its installer, with "3DS Development" ticked), run from devkitPro's MSYS2 shell, where
# DEVKITPRO is set: devkitPro's toolchain refuses Windows' own CMake. On Windows,
# build-3ds.cmd starts that shell for you. CMake downloads SDL3 with git.
#
# On the 3DS, copy FoxRV.3dsx to sdmc:/3ds/ and the original game's files (FOX.COM and the
# .SQZ files) to sdmc:/3ds/foxrv/game/.
#
# Extra arguments go to CMake's configure step.
set -e
cd "$(dirname "$0")"

if [ -z "$DEVKITPRO" ] || [ ! -f "$DEVKITPRO/cmake/3DS.cmake" ]; then
    echo "build-3ds.sh: devkitPro's 3DS toolchain not found (DEVKITPRO='$DEVKITPRO');" >&2
    echo "run this from devkitPro's MSYS2 shell, see the top of this file." >&2
    exit 1
fi
for tool in cmake git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build-3ds.sh: $tool not found (in devkitPro's MSYS2: pacman -S $tool)." >&2
        exit 1
    fi
done

# A new build folder: Ninja if there is one, else make. An existing one keeps its generator.
if [ ! -f build-3ds/CMakeCache.txt ]; then
    if command -v ninja >/dev/null 2>&1; then
        set -- -G Ninja "$@"
    else
        set -- -G "Unix Makefiles" "$@"
    fi
fi

cmake -S . -B build-3ds -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-3ds --parallel "$(nproc 2>/dev/null || echo 4)"
echo
echo "Built build-3ds/FoxRV.3dsx. On the SD card: FoxRV.3dsx in /3ds/, the original game's"
echo "files in /3ds/foxrv/game/ (see README.md)."
