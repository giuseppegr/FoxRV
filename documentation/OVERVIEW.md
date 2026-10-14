# Titus the Fox (DOS): technical overview

What this copy of *Titus the Fox* (Titus, 1991–92) in `TTF/` contains, and how each data format works. Everything here was found by reading the files and disassembling the game's executable.
The Python tools in `tools/` turn all of it into PNG, JSON and WAV files under `export/`.

**Confidence**: unmarked statements were checked against the data or the game code. Statements marked *(tentative)* are inferences that haven't been proven.

---

## 1. Files in `TTF/`

| File | What it is |
|---|---|
| `runme.bat` | Entry point. Runs `ffc sb`. |
| `ffc.com` (162 B) | A small loader. It hooks INT 10h, runs `FOX.COM`, and patches the game in memory by replacing a `CALL` with `NOP`s. This looks like a copy-protection bypass (crack). |
| `fox.com` (44 KB) | **The game.** It's packed with *ZIVPack (Eric Zmiro, 1991)*; see §3. |
| `titf_trn.com` | A third-party trainer, *"Titus Fox Infinite Lives by Phoenix/TPC"*. It hooks INT 21h and NOPs out the lives decrement. |
| `titus.exe` | **Not part of the game.** It's an ARJ archive from 1994 holding archivers, a "DOTT" program and `MM2CODES.DOC`. |
| `*.sqz` | Compressed game data (§2): levels, graphics, font, screens. |
| `swhere.mod`, `stable.bin` | **Never loaded by the game.** They're a ProTracker module ("somewhere") and a ~64 KB table, probably leftovers from the crack intro. |

This copy is therefore a cracked/scene release, not a pristine retail disk.

## 2. SQZ compression

Every `.sqz` file starts with a 4-byte header: `out_len = b0<<16 | b3<<8 | b2`, `type = b1`.

- **type `0x10` → LZW**:
  - Bits are read MSB-first.
  - Code width starts at 9 bits and grows to 12.
  - `0x100` = clear, `0x101` = end.
- **any other type → Huffman + RLE**:
  - Layout: `u16 tree_bytes`, then a tree of `u16` nodes. Bit `0x8000` marks a leaf.
  - Leaf `< 0x100` = literal byte.
  - `0x100` = repeat the last byte N times, where N is the next symbol.
  - `0x101` = repeat the last byte N times, where N is the next two symbols, **high byte first**.

  Only `sprexp.sqz` and `levela.sqz` use Huffman. `sprexp.sqz` uses the `0x101` code exactly once, and getting its byte order wrong corrupts every sprite after #331.

Implemented in `tools/sqz.py`. All 23 files decode to exactly their declared length.

## 3. The executable (`fox.com`)

**ZIVPack** is the game's own LZW wrapped around an EXE. After an `MZ` marker in `fox.com` (word-aligned) comes an ordinary SQZ-LZW stream. It decodes to the real **63,985-byte MZ executable**, which `tools/foxexe.py` saves as `export/fox_unpacked.exe`.

**Addresses in this document** are either:
- **image offsets**: the EXE after its 512-byte header. Code segment = image `0x200`.
- **DS offsets**: the data segment, segment `0x68F`, i.e. image `0x68F0`.

Game tables in the data segment. The 20-entry tables are indexed by level **file slot**: `0`–`9` → 0–9, `A`–`J` → 10–19.

| DS offset | Size | Content |
|---|---|---|
| `5057` | 16×3 | VGA palette (6-bit RGB). Colour 14 is replaced per level. |
| `50C8` | 20×3 | Per-level colour 14 |
| `50A0` | 20 | Map height (rows) per level |
| `508C` | 20 | Music track per level |
| `5104` | 20 strings | Level names |
| `523C` | 20 | Play order (1-based; `FF` = unused slot) |
| `6665` | 356×2 | Sprite (width, height) table, ending with `(0,0)` |
| `0515` / `0675` | lists | AdLib song channel pointers / instrument pointers (§8) |
| `0156` | 14×2 | AdLib sound-effect patch pointers |
| `0982` / `09A2` / `0B20` | | PC-speaker song pointers / tempos / PIT divisors |

## 4. Levels

### Play order

| # | File | Name | Music |
|---|---|---|---|
| 1 | `level0` | On the Foxy Trail | 12 |
| 2 | `levelj` | Looking for Clues | 11 |
| 3 | `level1` | Road Works Ahead | 12 |
| 4 | `level2` | Going Underground | 13 |
| 5 | `level3` | Flaming Catacombes | 13 |
| 6 | `level4` | Coming to Town | 12 |
| 7 | `level5` | Foxy's Den | 11 |
| 8 | `level6` | On the Road to Marrakesh | 13 |
| 9 | `level7` | Home of the Pharaohs | 10 |
| 10 | `level8` | Desert Experience | 3 |
| 11 | `level9` | Walls of Sand | 10 |
| 12 | `levelb` | A Beacon of Hope | 11 |
| 13 | `levelc` | A Pipe Dream | 13 |
| 14 | `levele` | Going Home | 11 |
| 15 | `levelg` | Just Married (ending) | 14 |

The unused slots are `A`, `D`, `F`, `H` and `I`. Their names are "BUG BUG BUG", except slot `F`, which is named **"Arrival in Paris"**: a level with no data file that looks cut. `levela.sqz` is a 320×200 EGA picture, not a level.

### Level file layout (decompressed)

`size = rows × 256 + 35828`. The map height in rows matches the table at DS:`50A0` for all 15 levels.

| Offset | Size | Content |
|---|---|---|
| 0 | rows×256 | Tile map: 256 tiles wide, 1 byte per tile (16×16 px tiles) |
| rows×256 | 32768 | Tileset: 256 tiles × 128 bytes (4 planes × 16 rows × 2 bytes) |
| +32768 | 3060 | Metadata ("tail"), below |

Tail layout:

| Tail offset | Size | Content |
|---|---|---|
| `000` | 3×256 | Per-tile flags: horizontal/wall, floor, ceiling |
| `300` | 40×6 | Objects: `u16 sprite` (low 13 bits) + flags, `u16 x`, `u16 y` (pixels) |
| `3F0` | 2 | Camera: soft lower limit row (§11d) |
| `3F2` | 4 | Player start `x, y` (pixels) |
| `3F6` | 50×26 | Enemies (§4a). `+04 = FFFF` = unused. |
| `90A` | 100×4 | Bonuses: bonus tile, replacement tile, `u16` map offset (`x + y×256`). The bonus tile is written into the map at load. |
| `A9A` | 2 | Camera: last column the camera may reach; `0x00EC` (236) = the whole map (§11d) |
| `A9C` | 20×7 | Gates: entrance (x,y), screen (x,y), exit (x,y), no-scroll flag (tile units) |
| `B28` | 10×20 | Elevators (§4b) |
| `BF0` | 4 | Finish `x, y` (pixels) |

At load (image `0x3112`) the whole tail is copied into the data segment at DS:`72A3`, so enemies live at DS:`7699` and elevators at DS:`7DCB`. The game then uses these copies directly as its runtime state.

### 4a. Enemies

The 26-byte record. Decoded per level in `export/entities/enemies.json`.

| Offset | Meaning |
|---|---|
| `+00` / `+02` | x, y (px) |
| `+04` | Animation key (low 13 bits): the enemy starts at the first word equal to it in the animation table (§5a). Bit 15 = initial flip. |
| `+06` | **Behaviour type** (low 13 bits), dispatched through 19 handlers at DS:`8EB5` (image `0x5B61`). The top bits are runtime state (hit / dead / event frame). |
| `+08` | Signed horizontal speed in px per frame (`x -= speed`, so positive moves left). Its sign is the facing: the animation step copies it into the flip bit, so the file's initial flip is overwritten on the first frame |
| `+0A` | Runtime animation pointer. The values in the files are stale pointers left by the level editor. |
| `+0C` | Knock-back given to the player on contact; 0 = harmless (handled at image `0x5C04`) |
| `+0E` | Runtime state byte |
| `+0F…+19` | Type-specific parameters (below) |

| Type | Behaviour | Parameters |
|---|---|---|
| 0, 1 | Patrol back and forth, ignoring walls and gravity (the two types behave identically) | `+0F` center x, `+11` range |
| 2 | Shooter: fires its next animation's frame as a projectile when the player is in range | `+0F` first delay, `+10` reload, `+11` range (14 bits) + direction lock (top 2 bits) |
| 3, 4 | Patrol, and leap up at a player passing overhead | center, range, `+13` leap height |
| 5, 6 | Patrol, and swoop vertically at the player | center, range, `+13` dive depth |
| 7 | Gravity walker that strikes on contact | `+0F/+11` home x/y, `+13` speed, `+17` detection range |
| 8 | Gravity walker that activates while off-screen | home, speed |
| 9 | Walker with a periodic action | home, speed, detection range |
| 10 | Alert at range, walks when the player comes 50 px closer | home, speed, alert range |
| 11 | Walker that attacks/throws within 64 px | home, speed, detection range |
| 12 | Bouncer (immortal): jumps up and down in place, e.g. lava fireballs | `+0F` jump power, `+13` pause |
| 13 | Leaps at the player | `+14` pause, `+17` range |
| 14 | Like 8, but immortal | home, speed |
| 15 | Static, immortal | – |
| 16 | Static, killable (not used by any level) | – |
| 17 | Dropper (immortal): drops objects on a player below | `+0F` range x, `+11` delay, `+15` range y |
| 18 | Zone guard: chases the player inside a rectangle, otherwise returns to its centre | `+0F/+11` half width/height, `+13` y speed, `+14/+16` centre |

Types "with home" teleport back to their home position once they've wandered far off-screen. DS:`50B4` holds per-level boss hit points. The boss flags by key (DS:`70C5`) are empty in the file. At start-up, image `0x26C9` fills them from a list at DS:`7255` of (first key, count) pairs ending with `FFFF`: keys `93`–`96`, `97`–`9B`, `9C`–`A0`, `A2`–`A6`, `B7`–`BB` and `E4`–`E7`.

| Level | Boss | Hit points |
|---|---|---|
| 3 Road Works Ahead | record 27, key `93` | 10 |
| 5 Flaming Catacombes | record 35, key `E4` | 4 |
| 7 Foxy's Den | record 10, key `9C` | 10 |
| 9 Home of the Pharaohs | record 26, key `B7` | 10 |
| 11 Walls of Sand | record 17, key `97` | 10 |

### 4b. Elevators

The 20-byte record. Updated by image `0x4028`; exported to `export/entities/elevators.json`.

| Offset | Meaning |
|---|---|
| `+00` / `+02` | Current x, y |
| `+04` | Sprite − 30 (low 13 bits); `FFFF` = unused |
| `+06` | Direction: 0 up, 1 right, 2 down, 3 left. It's reversed (`xor 2`) at the end of each run. |
| `+07` | Speed in px per frame. A value ≥ 8 disables the elevator at load. |
| `+0A` | Run length in frames |
| `+0C` / `+0E` / `+10` | Start x, y, direction |
| `+11` | Frame counter (runtime) |

- **Load** (image `0x3151`): a record with speed ≥ 8 gets `+04 = FFFF`. Several levels hold junk records that this switches off.
- **Update** (`0x4028`, first thing in every frame): all 10 records move, on screen or not. A direction other than 0–3 doesn't move and doesn't count. After the move, a record within 16 px left/right of the 320 × 192 screen, and from its top to 16 px below it, is copied into the display list at DS:`5648` (4 slots of 18 bytes, the last ones of the sprite list): sprite word `+04 + 1E`, x, y, and at `+9` a pointer back to the record.
- **Reset**: every death puts them back at the start with the start direction (`0x5905`, inside the enemy reset).
- **Sprites**: 90 (16 px) and 92 (32 px). Their hot spot is (0, 0), so (x, y) is the top-left corner of the platform and riders stand at y.

**Standing on one** (`0x4857`, from the collision routine when no floor was found, before platform objects):
- **Skipped** while rising (`vy < 0`) or dropping through (DS:`6510`). Only elevators in the display list count, so an elevator that isn't visible can't be stood on, and only with bit 13 of the sprite word set.
- **Test**: `|dx| < 64` and `|dy| < 16`, then a box overlap. Titus's box is sprite 0's: from `x − hot_x`, collision width 28; vertically 8 px from 6 px above the feet. The elevator's box is its collision box from its corner.
- **Effect**: feet on the platform (`y = elevator y`), `vy = 0`, landed (`0x4EF0`, which unlike a floor keeps the slipperiness). Then Titus moves with it: + speed right, − speed left, + speed down. Nothing is added going up, because the elevator has already moved this frame and `y` was just set from it.
- Objects and enemies never ride elevators.

### Map export and the coloured squares

`export/levels/<level>_map.png` renders each full map with the real palette. It also draws **coloured outline boxes** that the tool adds on top; they are *not* game graphics:

| Colour | Box | Marks |
|---|---|---|
| 🟩 **Green** (double box, ~26 px) | Player **start** position (tail `3F2`) |
| 🟥 **Red** (double box, ~26 px) | Level **finish** position (tail `BF0`) |
| 🟪 **Magenta** (20 px) | **Enemies** (tail `3F6`) |
| 🟨 **Yellow** (12 px) | **Objects**: pick-ups and throwables such as crates, balls and bottles (tail `300`) |
| 🟦 **Cyan** (16 px) | **Elevators** / moving platforms (tail `B28`) |
| 🟧 **Orange** (14 px) | **Gate entrances**: doors/teleports to another place in the level (tail `A9C`) |

Bonuses aren't marked; they're the tiles listed in the bonus table.

Each box sits on the entity's (x, y), with that point at the **bottom-centre** of the box. That reading of the coordinates is an assumption, but it looks right: enemies stand on platforms. Export without the boxes with `python export_levels.py --no-markers`.

`<level>.json` holds all the decoded tables plus the level's title, play number and palette. `<level>_tiles.png` is the 16×16 tileset sheet.

### 4c. Level codes

The 4-hex-digit level codes are **not stored anywhere**. `FOX.COM` computes them at run time from the **PC's BIOS ROM** and **CPU class**, so they differ between computers (and between DOSBox versions or CPU settings).

**CPU class** (image `0x129F`):
- `shl al, 32` stays 0 on an 8086/8088, which doesn't mask the shift count → **0**.
- Otherwise `xor eax, eax` runs on a 386+ → **3**.
- If that raises an invalid-opcode fault (caught with a temporary INT 6 handler), the CPU is a 286 → **2**.

**Code for level file slot `s`** (image `0x6782`; `B[a]` / `W[a]` = byte / word at `F000:a`):

```
ax  = W[s]
al += B[F000h + s]
ah -= B[F002h + s]                 ; sets the carry flag
ax  = ror(ax, cpu_class)           ; a rotate by 0 leaves the carry from the subtraction
ax += W[FFF0h] + W[FFF2h] + … + W[FFFEh]   ; 8 words with adc, carry chained
                                   ; (reset vector, BIOS date "mm/dd/yy", model byte)
code = rol(ax, s)
```

- **Display:** the code-reveal screen (image `0x533`) prints this for the current slot.
- **Entry:** the entry screen (image `0x47B`) tries slots 0→20 and jumps to the **first** match, refusing unused slots.
- **Collisions:** because the last step rotates by the slot number, slots 16 apart can get the same code if their BIOS bytes are equal.

`tools/levcodes.S` → `export/levcodes/LEVCODES.COM` (708 bytes) repeats both routines instruction for instruction. It prints every level's code for the machine it runs on, so run it under the same DOSBox profile as the game.

Output in the D-Fend Reloaded DOSBox (CPU class 3, BIOS date 01/01/92):

| Lv | Code | Lv | Code | Lv | Code |
|---|---|---|---|---|---|
| 1 | E1AB | 6 | 1ABE | 11 | 57C3 |
| 2 | 0D5F | 7 | 357C | 12 | 5F0D |
| 3 | C357 | 8 | 6AF8 | 13 | BE1A |
| 4 | 86AF | 9 | D5F0 | 14 | F86A |
| 5 | 0D5F | 10 | ABE1 | 15 | E1AB |

Here every code is a rotation of `E1AB`, because the per-slot BIOS bytes are identical. So level 2's code collides with level 5's and opens level 5, and level 15's collides with level 1's.

## 5. Palettes and graphics

- **In-game palette (VGA)**: 16 colours at DS:`5057`. When a level loads (image `0x30F3`), colour 14 is replaced from DS:`50C8`. Examples: orange for the flames in *Flaming Catacombes*, sand in the Egypt levels. Index 0 is transparent in sprites. EGA mode was deliberately not analysed.
- **Full-screen pictures**: `titus`, `titre` and `menu` are VGA (768-byte palette + 320×200 pixels). `titreega`, `menuega` and `levela` are EGA (4 sequential planes, 32000 bytes). Exported to `export/images/`.
- **Easter egg**: `levela.sqz` is a digitised photo of the developers, signed "ZMIRO", "GIL" and "KIKI". The game shows it, with song 9, when exactly **Ctrl + Alt + E** are held (image `0x1C2A`).
- **Sprites** (`sprexp.sqz`, 194,024 bytes): 356 headerless sprites one after another. Each is 4 sequential planes of `ceil(w/8) × h` bytes, with sizes from DS:`6665`. The set includes Titus's animations, enemies, items, the axe-man, the "GAME OVER"/"THE END" lettering and the wedding ending. Exported to `export/sprites/`, with `sheet.png` showing sprite index = row × 16 + column. Sprite-number offsets: enemies use *key + 101*, elevators *value + 30*, and objects use the sprite number directly.

### 5a. Animations

The word table at DS:`87D5`–`8EB5` (880 words) is split into **124 sequences**. It's stepped by image `0x66FF`; exported to `export/entities/animations.json`.

- `value & 0x1FFF` = sprite key (sprite = key + 101). Each word lasts one game frame, so repeats set the timing.
- `0x2000` = **event frame**: the behaviour code reacts to it, for example by firing.
- `0x55AA` = remove the enemy.
- A negative word = jump back that many bytes. The target can be in a different sequence; for example, an attack ends by jumping to a resting frame that holds itself with `-2`.
- An enemy's states use consecutive sequences: the behaviour steps to the next or previous sequence through image `0x66DF` / `0x66EF`.

## 6. Font

`fonts.sqz`: 41 glyphs, 8×12 px, 4 planes × 12 bytes. Glyph order is `0-9 ! ? . $ _ A-Z` (no lowercase).

The game's text routine (image `0x756`) draws glyphs **opaque**: each glyph's background is colour 1. Before a text screen the game loads its own palette from DS:`04C4` (image `0x58A`; `0x5AE` restores the game palette). In it, colour 1 is black and colours 5–15 run from light yellow to dark orange, so text is yellow on black. Under the in-game palette the same glyphs would show as blue and brown on white cells. It maps a character to glyph `ch − 0x30`, minus 2 more when the result is above 9. So the strings in the EXE use `<` for `!`, `=` for `?`, `>` for `.` and `@` for `_`; for example "CODE  @@@@" displays as `CODE  ____`. A space is drawn as a solid black cell. Exported to `export/font/`.

## 7. Video modes

The game supports CGA, EGA and VGA; a mode byte at `CS:0001` selects the drawing paths. The level and sprite colours documented here are the VGA ones.

## 8. Sound

The game's audio code is the block at image `0x0F00`–`0x19FF`. The timer (IRQ0) is reprogrammed to divisor `0x4000` = **72.8 Hz**; music advances one step per tick, and one tick is skipped every *tempo* interrupts.

**AdLib music** (play `0x12FF`, tick `0x14AB`):
- **Song table:** song *n* = the n-th `FFFF`-terminated list of 10 channel stream pointers at DS:`0515`, plus the n-th instrument list at DS:`0675`. The last word of the instrument list is the tempo.
- **Mode:** the OPL2 runs in **rhythm mode**, which gives 6 melodic channels plus bass drum, snare, tom, cymbal and hi-hat. Instruments ≥ 15 are drums.
- **Stream bytes:**
  - `00-7F`: note (`octave<<4 | note`; note 12 = rest). F-numbers start at 343 (middle C) and step by semitone.
  - `8x`: duration (`0x60 >> x` ticks).
  - `9x`: volume.
  - `Cx`: tie.
  - `Dx`: OPL channel.
  - `Ex`: instrument.
  - `F0`: call. `F1`: set loop count. `F2`: loop. `F3`: return. `F4`: jump. `FF`: end.
- **Instruments:** 11-byte patches (carrier 5 bytes, modulator 5 bytes, feedback/connection).

**PC speaker**: the same encoding with one voice. Divisors come from DS:`0B20`, shifted right by the octave. There's a PC version of every song except song 8.

**Sound effects**: 14 AdLib effects. Each is the bass-drum channel retriggered for 15 ticks with its own patch.

| Song | Used for (from the code at each call site) |
|---|---|
| 3, 10, 11, 12, 13, 14 | Level music (see the table in §4) |
| 0 | Level start card ("LEVEL n"), also after a respawn |
| 1 | Life lost |
| 2 | Game over |
| 4 | Level complete |
| 5 | Special bonus tile touched (wall flag 5, floor flag 12, ceiling flag 3) |
| 6 | Energy / extra-life bonus (bonus tiles `FD`–`FF`) |
| 7 | Level code revealed (wall flag 4 / floor flag 11). Flags 6 / 13 reveal the code of level 14. |
| 8 | Unused: no call site |
| 9 | Credits screen, and the hidden developer photo |
| 15 | Title picture (`TITRE.SQZ`) at start-up |

These events come from per-tile flags: special tiles are dispatched through handler tables at DS:`726F` (wall), `727D` (floor) and `7299` (ceiling).

| Sound effect | Event |
|---|---|
| 1 | An enemy is hit by a thrown object |
| 3 | The player throws an object |
| 4 | The player is hurt by an enemy or a projectile |
| 5 | The player is hit by a falling object |
| 9 | The player picks something up (an object or an enemy) |
| 12 | The player bounces off a bouncy object (ball, spring) |
| others | No direct call site found |

All of this is also in `export/entities/sound_events.json`.

Exported to `export/sound/`:
- `music/song_NN.dro`: the **exact** register stream (DOSBox Raw OPL v2, playable in AdPlug-based players).
- `music/song_NN_adlib.wav`: rendered by `tools/opl2synth.c`, an **approximate** OPL2 model. It is not a hardware-exact emulation.
- `music/song_NN_pcspeaker.wav`: the PC-speaker arrangement.
- `sfx/`: the 14 sound effects.
- `unused_mod/`: samples from the unused `swhere.mod`.

Looping songs are rendered for one loop; `--loops N` renders more.

## 9. Tools

All tools live in `tools/` and read from `TTF/` by default.

| Script | Output |
|---|---|
| `sqz.py` | SQZ decompressor (library; can also be run on files) |
| `foxexe.py` | Unpacks `fox.com` and reads its tables; running it prints the level table |
| `export_images.py` | `export/images/`: full-screen pictures |
| `export_levels.py` | `export/levels/`: maps, tilesets, JSON |
| `export_sprites.py` | `export/sprites/`: 356 sprites + sheet (`--level N` picks the palette) |
| `export_font.py` | `export/font/`: glyphs, sheet, sample |
| `titus_audio.py` | Ports of the AdLib and PC-speaker drivers; DRO writer (library) |
| `export_sound.py` | `export/sound/`: DRO and WAV files |
| `export_entities.py` | `export/entities/`: `enemies.json`, `elevators.json`, `animations.json`, `sound_events.json` |
| `export_driver_data.py` | `export/sound/adlib_ds.bin`: the 64 KB data segment, for the remake's AdLib driver (no numpy) |
| `export_player.py` | `export/entities/player.json`: Titus's animation table, per-sprite hot spots and collision boxes, object property tables, enemy animation words and tables, new-game values, the ending scene (§11g) |

Requirements:
- **Python 3:** the core tools use only the standard library.
- **numpy:** needed by `export_sound.py`.
- **gcc:** builds `opl2synth.c` automatically on the first run. MSYS2 works: put `C:\msys64\ucrt64\bin` at the **end** of `PATH` so MSYS's own Python, which has no numpy, isn't picked up instead.

## 10. Open questions

- **Sound effects:** the meaning of the effects with no direct call site (0, 2, 6–8, 10, 11, 13).
- **Hardware accuracy:** the WAV renders haven't been compared with real OPL2 hardware or a reference emulator.

## 11. Gameplay

Found by disassembling the player and camera code. Speeds are in **1/16 px per game frame**; positions are whole pixels with y growing downwards.

### 11a. Frame timing and input

- **Game frame:** the main loop (image `0x317`–`0x355`) runs once per page flip, and the flip (`0x3471`) first busy-waits until **2 timer ticks** have passed since the last frame (`0x12D3`). So the game runs at most at 72.826 / 2 = **36.41 Hz**, about 35 Hz on VGA because of the retrace waits. A slow frame simply slows the game down: there is no frame skip.
- **Order per frame:** elevators (`0x4028`), objects (`0x3BC7`), player (`0x434B`), enemies (`0x5B45`), projectiles, collisions, level checks, object animation (`0x39F5`), camera and tile animation (`0x4146`), draw, death/level-end checks (`0x3827`), music restart (`0x358`: when the song has ended, the level song is played again), flip.
- **Keyboard:** the INT 9 handler (`0xF51`) keeps a 128-byte key table at DS:`0440`. Each frame `0xA05` turns it, and the joystick on port 201h, into five flags:

| Flag | Meaning | Keys |
|---|---|---|
| `042D` | Up (jump, climb) | Up arrow, Q, keypad |
| `042E` | Down (crouch) | Down arrow, A |
| `042F` / `0430` | Right / Left | arrows, L |
| `042B` | Fire (pick up / throw) | Space, Enter, F11, joystick buttons |

  P pauses; F1 loses a life; F2 quits; F3 toggles PC speaker; Esc exits to DOS. Ctrl+Alt+Q trades 10 bonuses for a life.

### 11b. Player

The player record is at DS:`527C`: x, y (the middle of the feet line), sprite word (bit 15 = mirrored), vx, slipperiness shift (`5284`), last direction (`5285`/`5286`), action state (`5287`), animation pointer (`5288`), vy (`528A`). A new game starts with **2 spare lives and 16 energy** (image `0x2D7`); energy is capped at 16 (extra energy counts as a bonus, DS:`041A`) and lives at 9.

**State selection** (`0x43BD`): stunned (11) and knock-back (12/13) win; otherwise left/right gives walk (1); on a ladder, ladder (6); down gives crouch (5) or crawl (3, with a direction), but not in the air; up with the jump cooldown at 0 gives jump (2), and jumping from a crouch starts a 100-frame "sneak" timer during which idle/walk become states 9/10 (top speed 48); fire gives pick-up (7) from crouch/crawl or throw (8) while carrying. Carrying adds 16. Handlers are at DS:`6EFB`.

| Movement | Rule |
|---|---|
| Walk | `vx += 16 >> shift` while a direction is held, limited to ±64 (4 px/frame); ±32 crawling |
| Friction | `|vx| -= 12 >> shift` when not pushing |
| Jump | while up is held, for at most 3 frames: `vy = min(vy − 32, −48)`, i.e. −48, −80, −112. No gravity during take-off. Held: 50 px high; tapped: 9 px |
| Air | gravity `vy = min(vy + 16, 192)`, full air control (±64) |
| Landing | only when `vy ≥ 0` (every floor is one-way); `y &= ~15`; another jump only after 6 frames on the ground |
| Ladder | x snaps to the column centre, `vy = ±64` with up/down, 0 otherwise |
| Skid | sprite 4 while sliding ≥ 1 px/frame in the facing direction with no input |
| Idle | after 140 frames the yawn sprite 29, until frame 175 |

**Collision** (`0x4684`, after moving): death when `y > (rows+1)·16`. The floor is probed at x−4, then x+5 (only if the first found nothing), on the tile row `y >> 4`, through the floor-flag handlers; nothing → fall. While rising (`vy ≤ 0`) the ceiling flag of the tile two rows up is applied, and if the body tile is a wall the player is pushed 2 px toward a free side. Finally the wall flag of the tile at `x + 8·sign(vx)` on the body row is applied; rows above, up to the sprite's collision height (DS:`6BF8`), only trigger bonus and code tiles.

**Sprites** are placed (image `0x1E61`) with the hot-spot table at DS:`692F`: left edge at `x − hot_x` (`x − (width − hot_x)` when mirrored), top edge at `y + 1 − hot_y`. For most sprites `hot_y` equals the height, so the lowest row is the feet line. The game rewrites the table at load (`0x272F`) as `height − hot_y`, which gives the same result.

**Death** (`0x5BE7`, `0x3860`): a life is taken at once (or game over when none is left); song 1 plays and sprite 13 rises and falls for 60 frames (`vy` from 15 down to −16, `y −= vy`); then the level's objects and enemies are reset, Titus reappears at the respawn point with full energy, and song 0 plays before the level music. Game over: song 2, the "GAME OVER" letters slide in, back to level 1.

### 11c. Tile flags

| Value | Floor (DS:`727D`) | Wall (DS:`726F`) | Ceiling, low 7 bits (DS:`7299`) |
|---|---|---|---|
| 0 | none (fall) | none | none |
| 1 | solid | solid: undo the move, `vx = 0` | solid: stop rising, or force a crouch under it |
| 2 | slippery (shift 1) | bonus | ladder: grab it when jumping into it with no horizontal speed |
| 3 | slippery (shift 2) | deadly (unused) | checkpoint |
| 4 | slippery (shift 3) | level code | deadly |
| 5 | drop-through: crouch still for 15 frames to fall through | checkpoint | – |
| 6 | ladder | level 14's code | – |
| 7 | bonus | – | – |
| 8, 9, 10 | deadly (thrown objects: 8 destroys all but sprite `0x27`, 9 destroys all) | – | – |
| 11 / 12 / 13 | level code / checkpoint / level 14's code | – | – |

- **Bonus** tiles are the ones in the level's bonus table (written into the map at load). Touching one writes its replacement tile; bonus tiles `FD`–`FF` also give 1 energy and play song 6. Checkpoints play song 5 and move the respawn point; code tiles play song 7 and show the code screen.
- **Ladders**: a ladder top is solid for walking, crawling and idle. Crouching on it climbs down (sprite 14, y + 8); while the tile above is also a ladder the player hangs on it.
- **Ceiling bit 7** starts a group of 3 tiles animated together: every 4 frames each of them shows the next tile of the group (tables DS:`60E4`/`61E4`/`62E4`, built at `0x3176`).

### 11d. Camera

The camera (`0x4146`) is a tile position (DS:`5270`/`5272`) on a 20 × 12-tile screen. It never moves by pixels: each frame it moves **one tile** toward a target column/row, i.e. where the player should appear on screen.

- **Horizontal**: after 22 frames of the same state, walking at full speed sets target 2 (moving right, once past column 13) or 18 (moving left, once left of column 7), so the screen shows what's ahead; walking slower targets 2 or 18 by facing. Standing still for 33 frames looks ahead (3 or 17), or centres (10) when an enemy is within 200 px behind. Otherwise it recentres (10) when the player goes beyond columns 2–18. The right limit is tail word `A9A`, ignored once the player is 20 columns past it.
- **Vertical**: in the air or on a ladder, target row 3 when the player is at row ≥ 10 and 9 when at row ≤ 2; on the ground 10 and 9 with thresholds 11 and 2. Scrolling stops when vertical speed is 0. Tail word `3F0` is a soft lower limit L: the camera stays at or above row L (so the screen's bottom edge stays on row L+12) unless the player is below that line, and creeps down to it when the player comes within 112 px of it.
- **Gates** with the no-scroll flag freeze the camera (DS:`6502`).

### 11g. Finish, gates, level changes and the ending

`0x3AB5` runs every frame after the enemies.

**Finish**:
- **Conditions**: no boss left alive (DS:`64FD`). In file slot 9 ("Walls of Sand") the carry slot must also hold the idol (`38`/`39`).
- **Position**: the finish (tail `BF0`) must be Titus's tile column or the one to its left, and his tile row or the one above. The test is `x & ~15` against the finish x and x − 16, the same for y. So the "box" is 32 × 32 px.
- **Then**: song 4 plays and the game busy-waits until it ends (`0x19EB`), so everything freezes. Next comes a screen wipe (`0x2A11`). The next file slot is the one whose play number (DS:`523C`) is one higher. At `0x3827` the level-complete flag (DS:`6519`) wins over a death in the same frame.
- **Next level**: the status card (`0x373`), the energy back to 16, the reset (`0x398D`), and the new level loads with its music.

**Status card** (`0x373`; also after a death, `0x3D9`):
- **Sequence**: song 0, a wipe, then the text in the game font. "LEVEL" with the play number (column 13, line 60), the level name centred (line 76), "EXTRA BONUS" (line 120) and "LIVES" (line 132) with their numbers.
- **Numbers**: printed right-aligned in 5 places (`0x623`, leading zeros as spaces).
- **Bonus to lives**: while there are at least 10 extra bonuses, 10 of them are counted down, one every 15 frames, and become a life.
- **End**: it waits for a key or fire to be pressed and released (`0x5F2`).

**Gates** (`0x3B37`, only when the finish test failed):
- **Trigger**: while the drop-through timer runs (crouching still for 15 frames sets it), Titus's tile `(x >> 4, y >> 4)` is compared with each of the 20 entrances.
- **On a match**: speeds are cleared and the screen is wiped. Titus goes to the exit (`tile × 16`, so the left edge of the tile), and the camera to the gate's screen tile. During this move the camera limit (tail `A9A`) is set to the whole map. The no-scroll byte is copied to DS:`6502`.
- **Junk records**: some levels have entries outside the map (for example entrance row 246); they can never match.

**Death**: after the 60-frame fall, the game waits for song 1 to end (`0x38E8`). It shows the status card and plays song 0, then the level music.

**The ending**: file slot G ("Just Married") is not played.
- **Start**: `0x3AD6` sets the ending flag (DS:`6518`) every frame there, before the finish test, so the scene (`0x370E`) starts on its first frame. The level's music (song 14) keeps playing.
- **Setup**: all sprite records are cleared. Titus in his suit (sprite `157`, animation DS:`6609`) starts at x −100 and the bride (`151`, DS:`65E3`) at x 420, both at y 180.
- **Running**: each frame they run 3 px toward each other. The animation step `0x3811` advances first, then follows a jump word.
- **Meeting**: when the bride is within 28 px, she becomes the kissing couple (`815A`, mirrored) 24 px further left. Titus becomes a burst ring (DS:`661D`), 16 px higher, that sinks back 1 px per frame for 16 frames.
- **Heart**: a little heart (`163`) then hops along the path at DS:`663F` (x, y byte pairs, each held for one frame, repeated) until fire is pressed.
- **End**: "THE END" (`14F`/`150`) slides in like "GAME OVER", and `0x1C59` shows the developers' photo (`LEVELA.SQZ`) with song 9.

`tools/export_player.py` exports all of this as `ending` in `player.json`.

### 11e. Objects

The 40 level objects (crates, balls, pots, springs, the flying carpet, the skateboard…) are 18-byte records from DS:`52A0`, filled from the level's object list at load (`0x410C`) and again after every death. Each has x, y (feet), a sprite word (bit 15 mirrored, bit 13 "live": animates, raises landing dust, may fall through floors), vx, vy, a falling-frames counter (`+C`), spring rider fields (`+8`/`+9`) and a mode byte (`+B`: bit 0 falls through floors, bit 1 hurts the player). The **carry slot** at DS:`528E` holds what Titus carries, a thrown projectile, or a short effect (landing dust `2E`–`30`, impact star `2D`).

**Per-sprite tables** (sprites < `0x65`): DS:`6F19` flags — `01` platform (can be stood on), `02` bouncy, `04` heavy (thrown as an object, not a projectile), `08` fragile (falls through floors at vy ≥ 160), `10` never hurts enemies; DS:`6F60` terminal fall speed in px/frame. For example the crate `26` is `01`/13, the ball `27` is `07`/25, the spring `37` is `03`, the carpet `31` is `04`, the idol `38` is `14`.

**Object update** (`0x3BC7`, only while DS:`6511` > 0, which anything moving sets to 4): pushed back from the map edges; walls (`wall & 1`) **reflect** vx; friction 3 per frame on the ground, 1 in the air, vx 0 below 16; rising stops at any ceiling flag; a falling object lands on a tile whose floor flag is non-zero and not a ladder, or on a solid wall tile, and on platform objects; floor 9 destroys objects, floor 8 all but the ball; off the map (64 px below) they're removed. Gravity adds 16 per frame up to the sprite's terminal speed. A bouncy object landing gets `vy = 48 − vy`; one landing on a spring with vy > 64 is launched (`16 − vy/2`, and the spring `32 − vy/2`). An object that has fallen ≥ 10 frames and hits Titus stuns him (sfx 5).

**Titus and objects:**
- **Pick up** (state 7, `0x4B5B`): Fire while crouching takes the first object within 20 px vertically and 0–19 px in front (sfx 9); it leaves the list for the carry slot. (The original's horizontal box test compares Titus's x with itself, so it never fails.) Fire again while crouching puts it down.
- **Carry** (`0x450D`): overhead (`x + 2`, `y − height + 1`), or in front (`y − 4`, `x + 12` / `x − 10`) while crouching.
- **Throw** (state 8, `0x4AD6`, Fire after releasing it): with Up held the object is tossed straight up (vy −160); heavy objects fly as objects at ±168; others become a projectile in the carry slot that flies straight at 14 px/frame, through walls, until it leaves the screen (sfx 3).
- **Stand on** (`0x511F`, when no floor is found): platform objects carry Titus; a moving one moves him; standing still on the skateboard `5E` or block `53` for 10 frames sets it rolling at 3 px/frame; landing on a bouncy object faster than 48 bounces him back (`−(vy − 16)`, `−(vy + 48)` with Up held, none with Down; sfx 12). The carpet, ridden, flies at 6 px/frame where Titus faces for 200 frames.
- **Low ceilings** force a crouch and make Titus put the object down; dying drops it too. After a death the objects return to their places, except the idol (`38`/`39`), which returns to the last checkpoint reached while carrying it.

### 11f. Enemies

**Each frame** the main loop runs every enemy's behaviour handler (`0x5B45`, all 50 records, on screen or not), moves the enemy projectiles (`0x5AC8`), then the display pass (`0x5937`).

**The display pass** (`0x5937`):
- **Visibility:** an enemy is visible within 32 px left/right of the screen and from the top to 32 px below it.
- **Visible enemies:** it sets bit 13 of `+4`, steps the animation (`0x66FF`), adds a display record (DS:`5570`; the pick-up and the camera use these), and checks contact and hits.
- **Dead enemies:** once out of the window they are deleted (`+4 = FFFF`).
- **Units:** speeds and the vertical speeds of enemies are whole pixels per frame.

**Animation** (`0x66FF`):
- **Pointer:** `+A` points into the word table DS:`87D5`; at load it is set to the first word equal to the key (`0x58CC`).
- **Words:** a word ≥ 0 is a frame (sprite = key + `0x65`; bit `0x2000` makes it an *event frame*, which sets bit 13 of `+6` for the handler). A negative word jumps back that many bytes. `0x55AA` hides the enemy (`+4 = 05AA`: not drawn, not touchable) until the handler moves the pointer.
- **Sequence changes:** handlers change sequence with `0x66DF` (to the first frame of the next sequence) and `0x66EF` (to the last frame of the previous one).

**Contact** (`0x5C04`, skipped while Titus is being knocked back, and for types 15–17):
- **Overlap:** box overlap (`0x5295`: half the collision width from `x − hot_x`, the lower one's height − 3).
- **Turning:** the enemy turns round, except type 11 and key `B2`.
- **Damage** (`0x5B6F`), when `+C` ≠ 0: sfx 4, −2 energy, 24 frames of knock-back at `±(+C)` and vy −128, and the carried object is dropped.
- **Projectiles:** enemy projectiles hit the same way with knock-back 70, even during knock-back.

**Defeating enemies:** an object that moves (or has fallen for 10 frames) and isn't flagged harmless, or a thrown projectile, that touches a visible enemy:
- **Effect:** sets bit 15 of `+6` (sfx 1). Objects bounce back; a projectile becomes the impact star `2D` for 5 frames.
- **Death** (`0x5C58`, from the handler on the next frame): the enemy either jumps up and falls off the screen (vy −10, +1 per frame), or freezes on a corpse frame (DS:`6FD3`, by the high nibble of the class table DS:`6FDF`).
- **Immortal types:** 12, 14, 15 and 17 clear the hit bits every frame.
- **Bosses:** flagged by key in DS:`70C5` (filled at start-up, §4a). They share the level's hit-point pool (DS:`50B4`) and can't be hit again for 10 frames. A hit sets bit 14 of the display sprite, which the draw routine (`0x1EA5`) shows lit up; the remake keeps it blinking for those 10 frames. The finish stays closed while a boss is alive.

**Picking up enemies:**
- **Who:** enemies whose class low nibble isn't `F` can be picked up like objects (Fire while crouching, 0–19 px in front), but only from behind.
- **Effect:** the enemy is removed until the next death, and Titus carries a pose sprite (DS:`6FC5`).
- **Throwing:** thrown sideways it always flies as a projectile.

**Projectiles** (`0x6696`):
- **Spawn:** the two words after the shooter's event frame give the sprite (`word + 0x1E`) and the launch height. They fly at 11 px/frame toward Titus, through walls, until they leave the screen.

**Behaviours** (handler table DS:`8EB5`). Facing always follows the speed sign:

| Type | Handler | Behaviour |
|---|---|---|
| 0, 1 | `65BF` | `x −= speed`; turn back toward the centre beyond the range |
| 2 | `65F1` | On screen only. Faces Titus (or a fixed side). Shoots when within 24 px vertically and the range, after the first delay, then every reload |
| 3, 4 | `64C9` | Patrol. Leaps when Titus passes overhead in front within 48 px: initial vy is the smallest n with n(n+1)/2 ≥ the height, x moves 4·speed while rising |
| 5, 6 | `642E` | Patrol. Swoops ±2 px/frame vertically for the dive depth when Titus is level within 40 px |
| 7 | `62E0` | Wakes when Titus is level or below within range. Walks with gravity (fall speed up to 16), turns at walls, strikes when close. Lost beyond 640 px: waits until home is off screen, then teleports home |
| 8, 14 | `620F` | Wakes while out of sight (beyond 340 / 230 px). Walks with gravity, toward Titus at each landing |
| 9 | `60F2` | Walks without gravity; turns home over gaps; every 128 frames does an action, sometimes the other one |
| 10 | `5FCE` | Alert at range, walks once Titus is 50 px closer; ignores Titus while he sneaks (after a crouch-jump) |
| 11 | `5ECA` | Walks; within 64 px throws a projectile, then 20 frames of cooldown |
| 12 | `5E60` | Bounces up by the power (rise P(P+1)/2 px), then pauses — the lava fireball hides during the pause |
| 13 | `5DBB` | Leaps at Titus within range: up 55 px and down over 21 frames, then pauses |
| 15, 16 | `61F9` / `61FE` | Static: immortal / killable, both harmless |
| 17 | `5D4E` | Dropper: after its delay, drops an object on Titus below. The drop has bit 13 cleared and the object update removes such objects at once, so in this version the drops never appear |
| 18 | `5CB8` | Zone guard: chases Titus while he's inside its rectangle, otherwise returns to its centre |

Handlers that walk use the floor flag at the feet and the wall flag of the next column one row up. "Off screen" tests in the remake are widened by the extra columns and rows of the 16:9 view.
