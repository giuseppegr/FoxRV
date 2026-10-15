cmake_minimum_required(VERSION 3.24)

# The 3DS build (devkitPro's toolchain, see build-3ds.sh) runs devkitPro's compilers, Windows
# programs, from MSYS2's CMake, which misreads the C:/ paths in their dependency files: CMake
# finds the dependencies itself instead. project() reads this, so it's set before.
if(CMAKE_TOOLCHAIN_FILE MATCHES "3DS\\.cmake$" OR "$ENV{CMAKE_TOOLCHAIN_FILE}" MATCHES "3DS\\.cmake$")
    set(CMAKE_DEPENDS_USE_COMPILER OFF)
endif()
project(foxrv VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for editor IntelliSense
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- SDL3 (pinned release, built statically so the game is a single binary) ---------------
# devkitPro's compilers can't open the MSYS2 paths that precompiled-header files include.
if(NINTENDO_3DS)
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
endif()
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SDL3)

# AdLib sound needs the OPL emulator; without it (or on the 3DS, too slow there) only the
# PC speaker remains.
option(FOXRV_WITH_OPL "AdLib sound through the Nuked-OPL3 emulator" ON)
if(NINTENDO_3DS)
    set(FOXRV_WITH_OPL OFF CACHE BOOL "AdLib sound through the Nuked-OPL3 emulator" FORCE)
endif()

# --- Vendored single-file libraries (third_party/, licenses kept alongside) ----------------
if(FOXRV_WITH_OPL)
    add_library(thirdparty STATIC
        third_party/nuked-opl3/opl3.c
    )
    target_include_directories(thirdparty SYSTEM PUBLIC
        third_party/nuked-opl3
    )
    # Always optimised, also in Debug builds: the OPL emulator is the hot path for audio.
    target_compile_options(thirdparty PRIVATE -O2)
endif()

# --- Engine ----------------------------------------------------------------------------------
add_executable(foxrv
    src/main.c
    src/core/config.c
    src/core/assets.c
    src/core/sqz.c
    src/platform/video.c
    src/platform/gfx.c
    src/platform/font.c
    src/platform/input.c
    src/platform/audio.c
    src/platform/audio_speaker.c
    src/audio/adlib_driver.c
    src/audio/pcspk_driver.c
    src/audio/speaker.c
    src/audio/speaker_sfx.c
    src/game/anim.c
    src/game/level.c
    src/game/player.c
    src/game/camera.c
    src/game/objects.c
    src/game/elevators.c
    src/game/enemies.c
    src/game/ending.c
    src/game/menu.c
    src/game/options.c
    src/game/game.c
    src/game/selftest.c
)
set_target_properties(foxrv PROPERTIES OUTPUT_NAME FoxRV)    # FoxRV.exe / FoxRV; the target stays foxrv
target_include_directories(foxrv PRIVATE src)
target_link_libraries(foxrv PRIVATE SDL3::SDL3-static m)
if(FOXRV_WITH_OPL)
    target_sources(foxrv PRIVATE src/audio/synth.c src/platform/audio_opl.c)
    target_link_libraries(foxrv PRIVATE thirdparty)
    target_compile_definitions(foxrv PRIVATE FOXRV_WITH_OPL=1)
else()
    target_compile_definitions(foxrv PRIVATE FOXRV_WITH_OPL=0)
endif()
target_compile_options(foxrv PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter)

# Windows: the .exe's icon, and a GUI program, so no console window opens. --debug,
# --selftest and --dump-opl attach to the terminal they were started from (or open a console).
if(WIN32)
    enable_language(RC)
    target_sources(foxrv PRIVATE res/foxrv.rc)
    set_target_properties(foxrv PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Nintendo 3DS (devkitPro's 3DS.cmake toolchain, see build-3ds.sh): FoxRV.3dsx, with the
# icon and title for the Homebrew Launcher, and the default engine.ini in its romfs. The game's
# files and the saved settings live on the SD card (src/platform/n3ds.h).
if(NINTENDO_3DS)
    target_sources(foxrv PRIVATE src/platform/n3ds.c)
    ctr_generate_smdh(FoxRV.smdh
        NAME "FoxRV"
        DESCRIPTION "Titus the Fox engine"
        AUTHOR "FoxRV"
        ICON res/3ds/icon.png)
    ctr_create_3dsx(foxrv SMDH FoxRV.smdh ROMFS res/3ds/romfs)
else()
    # engine.ini is read from the working directory or next to the binary.
    add_custom_command(TARGET foxrv POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/engine.ini $<TARGET_FILE_DIR:foxrv>/engine.ini)

    # The original game's files (FOX.COM and the .SQZ files) go in game/ next to the binary.
    # They are not part of this repository; for development, point FOXRV_GAME_SOURCE at a copy
    # and every build puts it there.
    set(FOXRV_GAME_SOURCE "${CMAKE_SOURCE_DIR}/TTF" CACHE PATH "Folder with the original game's files, copied next to the binary")
    if(EXISTS "${FOXRV_GAME_SOURCE}/fox.com" OR EXISTS "${FOXRV_GAME_SOURCE}/FOX.COM")
        add_custom_command(TARGET foxrv POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${FOXRV_GAME_SOURCE} $<TARGET_FILE_DIR:foxrv>/game)
    endif()
endif()
