# Using Little Big Adventure 1

This document says how to build LBA1 Legacy for Linux, Windows and
DOS, run it on the game's files and set it up. The game's files come
from the PC CD-ROM of 1994, or from GOG's or Steam's package of the
game. `README.md` describes the project.

## Building

Each build goes into `build/OS-ARCH-CC`, the target that `OS`, `ARCH`
and `CC` name, `linux`, `amd64` and `gcc` unless given. `make` lists the
goals and the targets. `make dist` puts the target's distribution into
`dist/OS-ARCH-CC`, the programs and their licences, and its archive
beside it, `dist/lba1-OS-ARCH-CC.tar.gz` for Linux and `.zip` for
Windows and DOS, every file at the time of LBA.EXE's build, so that the
same folder gives the same bytes.

### Linux

The game needs SDL 3.2 or later. Install the dependencies:

```
sudo dnf install gcc make pkgconf-pkg-config SDL3-devel   # Fedora, RHEL
sudo apt install build-essential pkg-config libsdl3-dev   # Debian, Ubuntu
```

Build:

```
make all
```

The game is `build/linux-amd64-gcc/lba`, with `lba.cfg` beside it, its
default settings (Settings, below). `make all ARCH=386` builds the
32-bit program, which needs the 32-bit libraries (on Fedora,
`glibc-devel.i686`, `libatomic.i686` and `SDL3-devel.i686`, on Debian
and Ubuntu `gcc-multilib` and `libsdl3-dev:i386`), and `CC=clang`
builds with clang. `make test` runs the tests that need the tree alone.

### Windows

The game is `lba.exe`, which wants `SDL3.dll` beside it. On Linux,
mingw-w64 builds it with SDL's own archive for mingw-w64, which
`make fetch` downloads into `../lba1-tmp/downloads` and unpacks into
`../lba1-tmp/tools`, at the version `mk/fetch.mk` names:

```
sudo dnf install mingw64-gcc pkgconf-pkg-config curl tar   # Fedora
sudo apt install gcc-mingw-w64-x86-64 pkg-config curl      # Debian, Ubuntu
make fetch OS=windows
make all OS=windows
```

The game is `build/windows-amd64-gcc/lba.exe`, and make puts the
archive's `SDL3.dll` beside it. `ARCH=386` builds the 32-bit program,
with `mingw32-gcc` (`gcc-mingw-w64-i686` on Debian and Ubuntu).

On Windows itself, MSYS2's UCRT64 shell builds the program, which runs
in that shell:

```
pacman -S make curl mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf
make fetch OS=windows
make all OS=windows CROSS=
```

### DOS

```
make fetch OS=dos ARCH=386 CC=openwatcom     # JWasm, OpenWatcom 2, 10.0a's library
make fetch OS=dos ARCH=386 CC=watcom         # Watcom C 9.01d and 9.5b, MASM 6.11, TASM 3.1, 10.0a's library and linker
make all OS=dos ARCH=386 CC=openwatcom       # by OpenWatcom 2 and JWasm
make all OS=dos ARCH=386 CC=watcom           # by Watcom C 9.01d and 9.5b, MASM 6.11 and TASM 3.1, LBA.EXE's code
make all OS=dos ARCH=386 CC=watcom FIXES=1   # the same with the fixes
```

The program, `build/dos-386-openwatcom/lba0.exe`,
`build/dos-386-watcom/lba0.exe` or
`build/dos-386-watcom-fixes/lba0.exe`, takes the place of `LBA.EXE` in a
DOS install, beside the wave drivers built with it, `w_*.dll`, which
replace the CD's. The first and the last take the fixes of the game's
faults. `make test` on each holds the program and its drivers to their
sums (`dos/openwatcom.sums`, `dos/watcom.sums` and
`dos/watcom-fixes.sums`), runs the oracles, 32-bit programs which hold
the program's code to the port's C, and runs the program in DOSBox to
"No CD", without the game's files (`dos/nodata`).

`make fetch` puts the tools in `../lba1-tmp/tools`, or in `tools` of the
directory `LBATMP` names, each from its archive in `downloads` beside
it, which it downloads when it is missing and holds to the SHA-256 that
`mk/fetch.mk` names with its version and URL: JWasm, built from its
source, OpenWatcom 2, Watcom C 9.01 and 9.5b, installed from their
floppies by their own `INSTALL.EXE` in DOSBox Staging, 9.01 then brought
to its level D by Watcom's patches of 1992 (9.01d), MASM 6.11, expanded
from its first floppy by its own `DECOMP.EXE`, Turbo Assembler 3.1, from
the floppies of Borland C++ 3.1, and the library and the linker of
Watcom C/C++ 10.0a, from its CD: LBA.EXE links its `qsort` first, and
its `wlink` linked the CD's wave drivers. It needs these packages, and
DOSBox Staging's `dosbox` on the PATH, which Debian and Ubuntu take from
[its own release](https://www.dosbox-staging.org/releases/linux/):

```
sudo dnf install gcc glibc-devel.i686 libatomic.i686 make python3 curl xz 7zip dosbox-staging   # Fedora
sudo apt install build-essential gcc-multilib python3 curl xz-utils 7zip                        # Debian, Ubuntu
```

## Running

Start the port in the folder of an install, or beside it: it finds the
install from the current folder, else from its own, binds its
directories and its CD, and lists its own saves. Copy `lba.exe` and
`SDL3.dll` into the install's folder and start it there, as
`lba1legacy.exe` where the folder holds an `LBA.EXE` of its own, as
DotEmu's and a CD's do, and leave `lba.cfg` out of a folder that holds
an `LBA.CFG`: Windows takes the two names for one file, and the
program holds the text of `lba.cfg`. Each install, where it is found,
and the `-g` and `-c` that name it, from the store's folder for GOG's
and Steam's, `lba` standing for the program's path:

| Install | Found in | Named by |
|---|---|---|
| The CD, or a copy of its files | its root, or a drive's from any folder | `lba -g D:\LBA` |
| A DOS install, LbaWin's among them | its folder, its CD an image beside it or the one in a drive | `lba -g C:\LBA -c D:\` |
| GOG's or Steam's Classic | its folder | `lba -g Common -g CommonClassic` |
| GOG's DOS copy | `Speedrun\Windows`, its CD `LBA.DAT` | `lba -g Speedrun\Windows -c Speedrun\Windows\LBA.DAT` |
| Steam's DOS copy | `DotEmu\original`, its CD `LBA.DAT` | `lba -g DotEmu\original -c DotEmu\original\LBA.DAT` |
| DotEmu's Enhanced Edition | Steam's `DotEmu` | |
| An image of the CD, a cue sheet and its files or an ISO | its folder | `lba -c LBA.DAT` |

Its own files, the saves, the voices it keeps and the settings once it
changes one, go into its preferences: `~/.local/share/lba1` on Linux
(`$XDG_DATA_HOME/lba1` where it is set) and
`%APPDATA%\Adeline\Little Big Adventure` on Windows. It leaves the
install's files as they are. On Windows, a folder whose name holds
letters outside ASCII works from Windows 10 version 1903 on.

The options name the directories and the display:

```
lba [-g gamedir]... [-c cd]... [-p prefdir] [-D WxH] [-R hz]
```

- `-g` names a game directory, `LBA` on the CD, its files copied or
  the disc mounted, up to eight read in turn, in place of the search,
  the install's saves left out.
- `-c` names a CD: its root, or an image of the CD, a cue sheet with
  its BIN, WAVE, MP3 or Ogg Vorbis files, or an ISO, up to eight read
  in turn, each file from the first that holds it. `-c` alone keeps the
  search, its CDs in place of the CD the search finds, and where the
  search finds no install, plays the first of them that holds the CD's
  `LBA` directory, an image or a directory, whole.
- `-p` names the preferences' folder.
- `-D` takes the display as W by H pixels, which the wide screen takes
  its shape from, and `-R` its refresh as `hz` times a second, 1 to
  1000 or 0 for unknown, which `DisplaySync` paces the game and its
  presents by (below), where the system gives them wrong.

With `-g` the CD defaults to the first game directory's parent. On
Linux, write the paths with slashes, and the CD's mount point for
`D:\`. A start that ends with "No CD" after the logo needs the CD:
give `-c` its root, or copy its `FLA` into the game's directory.

## Settings

The game reads its settings as the 1994 game read `LBA.CFG`, a
`Key: value` a line, from one file: the first of the preferences' copy,
the game's directories' `LBA.CFG` and the `lba.cfg` beside the program.
That `lba.cfg` holds the default settings, a comment giving each of
its keys' values, and the program holds its text, read when the file
is missing. To change a setting, copy `lba.cfg` into the preferences and
edit it. The option menu writes the volumes, the languages, Display
text, Hit walls, Fullscreen display and Widescreen display into the
preferences' copy, which its first change makes as `LBA.CFG`, and the
option menu's Controls the bindings, `Input0_1` to `Input26_2`,
two keys for each of the 27 inputs, and `GInput0_1` to `GInput26_2`,
two of a pad's inputs, which a missing key leaves at the Classic's.

An install's own `LBA.CFG`, the CD's or a DOS install's, comes before
`lba.cfg`. The CD's turns the sound card off: copy `lba.cfg` into the
preferences to play the CD's files with it. LBA1 Legacy adds twelve
keys:

| Key | Default | |
|---|---|---|
| `LbaWinSettings` | `212` | Hit walls, the W key, the fades and more (below) |
| `LbaWinFullScreen` | `1` | `1` the whole display, `0` a window |
| `SaveAnywhere` | `ON` | `ON` saves the game as it runs (below) |
| `MusicTracks` | `Auto` | where the CD's tunes come from (Music, below) |
| `MusicJingles` | `Auto` | where the other tunes come from (Music, below) |
| `WideScreen` | `ON` | `ON` the screen 480 lines high and as wide as the display's shape, the scene filling it and the menus keeping 640x480 in its middle, `OFF` 640x480, switched by the option menu's Widescreen display on a display wider than 4:3 |
| `SkipLogo` | `ON` | `ON` lets a key end Adeline's logo at the start, as the Classic does |
| `SmoothScaling` | `ON` | `ON` scales the screen smooth through the linear filter, as the Classic draws it, `OFF` sharp, each of the game's pixels the same size |
| `IntegerScaling` | `OFF` | `ON` scales the screen by whole multiples of itself, black around it, `OFF` as large as the window takes it |
| `DisplaySync` | `ON` | `ON` paces the game by the display's refresh where it lies within 1% of the VGA's, and shows each picture on the display's vertical sync, `OFF` keeps the VGA's pace and shows the pictures as they come |
| `DesiredDisplayNum` | `0` | the display the game opens on, numbered from `0` as the Classic numbers them, `0` the primary, which a number below `0` or past the last takes |
| `GamePad` | `ON` | `ON` plays with the Classic's controls, its bindings of the keys and of a pad, its shortcuts and its previous and next behaviour, set on the option menu's Controls, `OFF` with the 1994 game's keys alone |

A key the file leaves out takes its default, as each of the twelve does
in an install's own `LBA.CFG`.

`LbaWinSettings` holds the switches of LbaWin and the Classic in one
number, the sum of their values. The default, `212`, is
4 + 16 + 64 + 128. The 1994 game's `210` is 2 + 16 + 64 + 128.

| Value | Switch | |
|---|---|---|
| `1` | Original game play | turns Hit walls and the fades on and W off at each start, until the option menu's Hit walls takes 1 away |
| `2` | Hit walls | on in 1994, costs Twinsen a life point when he runs into a wall in sporty behaviour. The option menu's Hit walls switches it |
| `4` | W, the action key | makes W do in every behaviour what Space does in the normal one: act, talk, search |
| `8` | Screen saver | keeps it off, as LBA1 Legacy does whatever 8 says |
| `16` | Fades | fades the screen out and in at each change of scene, as in 1994. Take 16 away for a scene that comes in at once |
| `32` | Screen moves | makes the arrows walk Twinsen the way they point on the screen, Up to its top and Left to its left, turning him on the way |
| `64` | Alt+F4 | on Windows, keeps Alt+F4 from closing the game, as LbaWin and the Classic do |
| `128` | Windows keys | on Windows, refuses them on the whole display, as LbaWin and the Classic do |

With `SaveAnywhere: ON`, S or F8 saves the game where it stands and L
loads a save, from a list that shows each save's scene. Escape opens a
menu over the game: Resume Game, New Game, Load Game, Save Game,
Options Menu, Quit. In a menu, Escape goes back to the menu before. The
game saves itself as each scene starts, and at the next run Resume Game
goes on from where Escape left the game.

## Music

Nine tunes, the main menu's among them, are the CD's audio tracks. The
others, the jingles, are sequences that an emulated sound card plays,
the OPL3 by default. Two keys choose where each kind comes from:

| Key | Value | Plays |
|---|---|---|
| `MusicTracks` | `CD` | the CD's tracks: an image's, or `track02.wav` to `track10.wav` in the CD's root |
| | `Midi` | the sequences in the tracks' place |
| | `Classic` | the Classic's MP3 files, in `Common/Music` under the CD's root, or in their place DotEmu's OGG files of the same names |
| | `Auto` | the default, the first the CD's root holds: the Classic's files, the tracks, the sequences |
| `MusicJingles` | `Midi` | the sequences |
| | `PlayStation` | the PlayStation's recordings: its disc's image, or `JINGLES/J01MID.WAV` to `J18MID.WAV` in the CD's root |
| | `CD` | each jingle cut from the track its music comes from |
| | `Classic` | the Classic's MP3 files, in `Common/Midi` under the CD's root, or in their place DotEmu's OGG files of the same names |
| | `Auto` | the default, the first the CD's root holds: the Classic's files, the cuts, the recordings, the sequences |

An image of the whole CD, a cue sheet and its BIN files, plays its own
tracks. So does the PlayStation's disc, whose image holds the same
tunes and its recordings of the jingles: name it before the PC CD,
which gives the voices and the movies.

```
lba -c playstation.cue -c /path/to/cd
```

GOG's and Steam's image of the North American CD, `LBA.DAT`, starts
tracks 3 to 10 2 s late, as its cue sheet places them: tunes 2 to 9
begin 2 s in, and tunes 1 to 8 end on the next one's first 2 s. In a
directory the tracks are WAV files, 16-bit stereo at 44100 Hz, and the
PlayStation's recordings `JINGLES/J01MID.WAV` to `J18MID.WAV`, which
`tools/cdtracks.py` writes from an image of the whole CD, or of the
PlayStation's disc, whose tracks 2 to 9 and 27 hold the same tunes:

```
python3 -I tools/cdtracks.py image.cue /path/to/cd
```

With `MidiDriver: A32GFX.DLL` and `MidiType: Midi`, the sequences play
on an emulated Sound Blaster AWE32. It needs the card's sound ROM as
`awe32.raw` in the preferences or the game's directory, the dump in
86Box's ROM set (`sound/creative/awe32.raw`).

## Saves

The saves go into the preferences. The game also lists the saves of
the install it starts in, where that version keeps them, and loads
them as they are: LbaWin's `Save`, the Classic's `Save` and, on
Windows, its folder in Saved Games, the Enhanced Edition's `saves`. A
save of another version loads as it is, from the game's directory or
copied into the preferences.

`lbasave`, built beside `lba`, writes a save as another version writes
one, under the name that version gives a new save:

```
build/linux-amd64-gcc/lbasave -t classic S2292.LBA dir
```

| `-t` | The version | Its saves |
|---|---|---|
| `lba` | the 1994 game | its directory |
| `lbawin` | LbaWin | `Save` in its directory |
| `classic` | the Classic | `Saved Games\2point21\tlba-classic\Save` in the user's profile |
| `dotemu` | DotEmu's, Android and the Enhanced Edition | `Android/data/com.dotemu.lba/data`, or `DotEmu\saves` in the install |
| `legacy` | LBA1 Legacy | the preferences |

`lbasave -l` lists a save's fields.

## The data's fixes

LBA1 Legacy mends the game's data as it loads it: the scenes, the
grids and the texts, as the later versions and the European CD mend
them. `tools/lbapatch.py` writes the same fixes into the files, for
another version of the game, LBA.EXE among them:

```
python3 -I tools/lbapatch.py -l /path/to/LBA
python3 -I tools/lbapatch.py -o fixed /path/to/LBA
```

`-l` lists the data files an install holds and what each is. `-o` writes
into `fixed`, which must exist, the files the fixes change: `SCENE.HQR`,
`LBA_GRI.HQR` and `TEXT.HQR`. The game's directories are named as for
`lba -g`, each in turn: `Common CommonClassic` for the Classic. Another
version takes the files in place of its own, its originals kept aside,
and LBA1 Legacy reads them first: `lba -g fixed -g /path/to/LBA`. A
save of the running scene (`SaveAnywhere`) keeps a sum of its scene's
data: one made before the fixes loads at its scene's start.

`-s` picks the sets in place of the three, `scenes`, `grids` and
`texts`, or one of these:

| Set | What it writes |
|---|---|
| `face` | Twinsen's face as the PlayStation version remodelled it, on the PC's head |
| `face-full` | the PlayStation version's head, hair and hands, its spheres made shapes |

`face` and `face-full` read the player's own PlayStation disc, the
European one (`SLES_006.98`): its `TEXT.HQR`, `GAME.HQR` and
`SCENES.BLK` copied into the directory `-p` names, as `tools/isocat.py`
copies them out of its image:

```
python3 -I tools/isocat.py -o ps playstation.cue /TEXT.HQR /GAME.HQR /SCENES.BLK
python3 -I tools/lbapatch.py -s face -p ps -o fixed /path/to/LBA
```

With `-p`, `texts` takes from the disc the 13 texts LBA1 Legacy takes
whole from the PlayStation version, in place of the tree's copies.
