<p align="center"><img src="res/foxrv_512.png" alt="FoxRV" width="192"></p>

# FOXRV

A new engine for **Titus the Fox: To Marrakech and Back** (Titus, 1992, DOS), written in C17 with SDL3.

It plays the original levels with the original rules, measured from the game's own code:
- physics, enemies, objects and elevators;
- the camera; 
- AdLib music and sound effects, through an emulated OPL chip.

It also brings some modern comforts:
- widescreen views (16:9 shows more of the level);
- square, integer-scaled pixels;
- smooth display on high-refresh monitors;
- gamepads;
- optional Scale2x/Scale3x art smoothing.

**No game data is included.** You need your own copy of the DOS game.

## Game files

Put the original game's files in a folder named `game` next to the executable:

```
FoxRV            (FoxRV.exe on Windows)
engine.ini
game/
    FOX.COM
    FONTS.SQZ  LEVEL*.SQZ  MENU.SQZ  SPREXP.SQZ  TITRE.SQZ  TITUS.SQZ
```

File names are matched ignoring case. Nothing is extracted to disk: the engine decodes the files in memory when it starts and when a level loads. The folder can be changed with `game_dir` in `engine.ini`.

## Building

You need CMake 3.24+, a C17 compiler (GCC or Clang) and git. SDL3 is downloaded and built statically by CMake.

The scripts in the root build in release mode into `build/`; extra arguments go to CMake:

```
build.cmd        Windows: MinGW-w64 GCC from MSYS2 (C:\msys64 is found even if not on PATH)
./build.sh       Linux: the packages SDL3 needs are listed at the top of the script
```

Or by hand:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The Windows build is a single `FoxRV.exe` that needs no DLLs besides Windows' own.

If `-DFOXRV_GAME_SOURCE=/path/to/your/game` is given, every build copies that folder to `build/game`. By default it is `TTF/` in the source tree. The game files are git-ignored.

## Running

```
build/FoxRV              intro, then the main menu
build/FoxRV 5            straight into level 5 (play order 1-15)
build/FoxRV --selftest   headless checks of the physics against the original's values
build/FoxRV --debug      print the engine's log (with any of the above)
```

The game prints nothing unless `--debug` is given. On Windows it is a GUI program with no console window; `--debug`, `--selftest` and `--dump-opl` print to the terminal they were started from, or open a console if there is none. PowerShell doesn't wait for GUI programs, so to wait for the result and get the exit code, pipe the output: `build/FoxRV --selftest | Out-Host`.

Settings live in `engine.ini`: video, audio, controls, camera, difficulty and the original's physics constants. Every key is optional.

The frame rate follows the display (`vsync = 1`). Where the driver ignores vsync, as with Wine on Android (Winlator), the game notices within a second and caps itself at the display's refresh rate; `max_fps` sets a lower cap, e.g. 30 to save battery. The game itself always runs at its original speed.

Gamepads (Xbox layout) by default: the D-pad or left stick moves and climbs, A jumps, B picks up and throws, Start pauses, Back opens the menu. Unlike the keyboard's Up, which jumps as in 1991, up on a pad doesn't jump (a worn stick or D-pad would trigger it by mistake), except to grab a ladder above; `pad_up_jumps = 1` in `[input]` makes it jump too. For worn controllers that slip into up, `pad_up_threshold` (how far the stick must be pushed up, 75% by default) and `pad_up_delay` (how long up must be held, 3 game frames, about 80 ms) can be raised, also in Options.

**Options** on the main menu sets the difficulty and maps a controller: *Map controller* asks for each action in turn (press a button, or push a stick or trigger; Esc cancels), and *Reset controller* restores the default layout. Both are saved to `engine.ini`. The keyboard layout is fixed there and only shown. `fullscreen = 1` in `[video]` starts in full screen; F11 or Alt+Enter switch at any time.

## Repository

| Path | Contents |
|---|---|
| `src/` | The engine |
| `src/core/fox_ds.h` | Where the game's tables are in FOX.COM's data segment |
| `documentation/OVERVIEW.md` | How the original's files and code work |
| `tools/` | Optional Python research tools that export the game's data as PNG, JSON, WAV and DRO files. Not needed to build or play. |
| `res/` | The FoxRV icon, original pixel art drawn by `tools/make_icon.py` (which also writes `src/platform/icon_pixels.h`) |
| `third_party/` | Nuked-OPL3, with its license |
