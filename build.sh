#!/bin/sh
# Builds FOXRV in release mode on Linux: build/FoxRV, with engine.ini next to it.
#
# Needs CMake 3.24+, a C compiler, git (CMake downloads SDL3 with it) and the development
# files SDL3 builds its video, audio and gamepad support from. For example:
#   Debian / Ubuntu:
#     sudo apt install build-essential cmake git ninja-build pkg-config libx11-dev libxext-dev \
#       libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
#       libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev \
#       libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev
#   Fedora:
#     sudo dnf install gcc cmake git ninja-build pkgconf libX11-devel libXext-devel \
#       libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXScrnSaver-devel \
#       libXtst-devel libxkbcommon-devel wayland-devel wayland-protocols-devel libdecor-devel \
#       mesa-libEGL-devel mesa-libGL-devel alsa-lib-devel pulseaudio-libs-devel \
#       pipewire-devel systemd-devel dbus-devel
#
# Extra arguments go to CMake's configure step, e.g.
#   ./build.sh -DFOXRV_GAME_SOURCE=~/games/fox
#
# It also packs a release zip, build/FoxRV-<version>-linux-<arch>.zip, for GitHub's
# Releases: a FoxRV folder with the binary, engine.ini and an empty game/ folder. The game's
# own files in build/game are never packed.
set -e
cd "$(dirname "$0")"

for tool in cmake git cc; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build.sh: $tool not found; see the top of this file for what to install." >&2
        exit 1
    fi
done

# A new build folder: Ninja if there is one, else make. An existing one keeps its generator.
generator=
if [ ! -f build/CMakeCache.txt ] && command -v ninja >/dev/null 2>&1; then
    generator="-G Ninja"
fi

# shellcheck disable=SC2086  # $generator is two words or none
cmake -S . -B build $generator -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --parallel "$(nproc 2>/dev/null || echo 4)"

# The release zip, assembled in its own folder so build/game (the game's files) stays out.
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' build/CMakeCache.txt)
zip_name="FoxRV-$version-linux-$(uname -m).zip"
rm -rf build/release "build/$zip_name"
mkdir -p build/release/FoxRV/game
cp build/FoxRV engine.ini build/release/FoxRV/
if command -v strip >/dev/null 2>&1; then
    strip build/release/FoxRV/FoxRV
fi
(cd build/release && cmake -E tar cf "../$zip_name" --format=zip FoxRV)

echo
echo "Built build/FoxRV. Put the original game's files in build/game (see README.md)."
echo "Release: build/$zip_name"
