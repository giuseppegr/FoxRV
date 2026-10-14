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
foxrv            (foxrv.exe on Windows)
engine.ini
game/
    FOX.COM
    FONTS.SQZ  LEVEL*.SQZ  MENU.SQZ  SPREXP.SQZ  TITRE.SQZ  TITUS.SQZ
```

File names are matched ignoring case. Nothing is extracted to disk: the engine decodes the files in memory when it starts and when a level loads. The folder can be changed with `game_dir` in `engine.ini`.

## Building

You need CMake 3.24+, a C17 compiler and git. SDL3 is downloaded and built statically by CMake.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If `-DFOXRV_GAME_SOURCE=/path/to/your/game` is given, every build copies that folder to `build/game`. By default it is `TTF/` in the source tree. The game files are git-ignored.

## Running

```
build/foxrv              intro, then the main menu
build/foxrv 5            straight into level 5 (play order 1-15)
build/foxrv --selftest   headless checks of the physics against the original's values
```

Settings live in `engine.ini`: video, audio, controls, camera, difficulty and the original's physics constants. Every key is optional.

## Repository

| Path | Contents |
|---|---|
| `src/` | The engine |
| `src/core/fox_ds.h` | Where the game's tables are in FOX.COM's data segment |
| `documentation/OVERVIEW.md` | How the original's files and code work |
| `tools/` | Optional Python research tools that export the game's data as PNG, JSON, WAV and DRO files. Not needed to build or play. |
| `third_party/` | Nuked-OPL3, with its license |
