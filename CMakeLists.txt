cmake_minimum_required(VERSION 3.24)
project(foxrv VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for editor IntelliSense
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- SDL3 (pinned release, built statically so the game is a single binary) ---------------
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

# --- Vendored single-file libraries (third_party/, licenses kept alongside) ----------------
add_library(thirdparty STATIC
    third_party/nuked-opl3/opl3.c
)
target_include_directories(thirdparty SYSTEM PUBLIC
    third_party/nuked-opl3
)
# Always optimised, also in Debug builds: the OPL emulator is the hot path for audio.
target_compile_options(thirdparty PRIVATE -O2)

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
    src/audio/adlib_driver.c
    src/audio/synth.c
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
target_link_libraries(foxrv PRIVATE SDL3::SDL3-static thirdparty m)
target_compile_options(foxrv PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter)

# Windows: the .exe's icon, and a GUI program, so no console window opens. --debug,
# --selftest and --dump-opl attach to the terminal they were started from (or open a console).
if(WIN32)
    enable_language(RC)
    target_sources(foxrv PRIVATE res/foxrv.rc)
    set_target_properties(foxrv PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

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
