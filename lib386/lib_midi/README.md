# LIB_MIDI

LIB_MIDI plays the game's XMIDI music through AIL/32, the Audio
Interface Library for 32-bit DOS by John Miles (Miles Design, Inc.).

ail32/ holds release 1.05 of AIL/32 (REL105, 1993-11-18) as John Miles
published it in AIL2.ZIP, each file byte for byte. ail32/license,
AIL2.ZIP's READ.ME, is his statement of 2000-05-26 making it
"open-source freeware, usable by anyone for any purpose".

The game links ail32.obj as REL105 builds it for Watcom C and DOS/4GW
(ail32/a32wr.mak): every function of it is in the shipped LBA.EXE byte
for byte. The drivers (A32*.DLL) are loaded at run time from the game's
directory, and the CD carries its own.
