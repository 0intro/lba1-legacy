# oracle.mk builds the oracles into build/oracle, 32-bit, at a fixed
# address, what they run mapped below them: oracle, LBA.EXE's own
# functions against ours (oracle.c), fmoracle, the CD's XMIDI driver
# for the OPL3 against sys/xmidi.c and sys/yamaha.c (fmoracle.c),
# aweoracle, its driver for the AWE32 against sys/xmidi.c and sys/awe.c
# (aweoracle.c), waveoracle, its samples' driver for the Sound Blaster
# 16 against sys/wave.c, and the other cards' on checks of their own
# (waveoracle.c), the drivers' C built as the port builds it, cdoracle,
# a DOS build's cdrom.asm on a model of MSCDEX against sys/cd.c on the
# same discs (cdoracle.c), keyoracle, its keyboard.asm's interrupt
# handler on a model of the 8042 and the BIOS against sys/keyb.c
# (keyoracle.c), timeoracle, its timer on a model of the PIT against
# sys/timer.c (timeoracle.c), vgaoracle, its screen on a model of VESA
# and the VGA against sys/video.c (vgaoracle.c), fileoracle, its files
# on a model of DOS's int 21h against sys/files.c (fileoracle.c),
# mixoracle, its mixer through the CD's M_SB16.DLL on a model of the
# CT1745 against sys/sound.c (mixoracle.c), memoracle, its memory over
# Watcom's heap and DPMI against sys/mem.c (memoracle.c), ailoracle, its
# music through AIL/32 and the CD's A32SP2FM.DLL on a model of the OPL3
# and the PIT against sys/midi.c (ailoracle.c), the eight on pc.c's PC,
# asmoracle, a DOS build's assembly against its translations into C
# (asmoracle.c, abi.c), asmoracle-asan, the same under
# AddressSanitizer, lba2oracle, LBA2's packer of the saves and its
# search of the nearest colour against ours (lba2oracle.c), LBA2's side
# built for oraclecheck alone, from LBA2's sources (LBA2) with JWasm.
# On a DOS target with the fixes, make test runs oracle, asmoracle,
# waveoracle and the eight but mixoracle and ailoracle, which make check
# adds, and on LBA.EXE's code (CC=watcom without FIXES) oracle,
# waveoracle and memoracle, make check adding mixoracle.

ORACLE=build/oracle
# -Werror lies in ORACLECC: a build that gives a compiler of its own
# takes a warning and goes on.
ORACLECC=gcc -Werror
ORACLECFLAGS=-m32 -std=c99 -Wall -Wextra -pedantic -O2 -g -mstackrealign -D_DEFAULT_SOURCE -DCDROM=1 -Isys
ORACLELDFLAGS=-m32 -no-pie
ORACLEPORTFLAGS=-funsigned-char -fno-strict-aliasing -fwrapv

ORACLESRC=\
	tools/oracle/oracle.c\
	sys/rand.c\
	sys/qsort.c\

FMSRC=\
	tools/oracle/fmoracle.c\
	tools/oracle/dll.c\
	tools/oracle/suite.c\
	sys/xmidi.c\
	sys/yamaha.c\
	sys/oplio.c\
	lib386/lib_sys/expand.c\

AWESRC=\
	tools/oracle/aweoracle.c\
	tools/oracle/dll.c\
	tools/oracle/suite.c\
	sys/xmidi.c\
	sys/awe.c\
	sys/awedata.c\
	sys/emu8k.c\
	sys/emu8kfx.c\
	sys/emu8ktab.c\
	lib386/lib_sys/expand.c\

WAVESRC=\
	tools/oracle/waveoracle.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/wave.c\
	lib386/lib_sys/expand.c\

CDSRC=\
	tools/oracle/cdoracle.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/cd.c\
	sys/files.c\
	sys/cdimage.c\
	sys/compat.c\
	lib386/lib_sys/expand.c\

KEYSRC=\
	tools/oracle/keyoracle.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/keyb.c\
	lib386/lib_sys/expand.c\

TIMESRC=\
	tools/oracle/timeoracle.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/timer.c\
	lib386/lib_sys/expand.c\

FILESRC=\
	tools/oracle/fileoracle.c\
	tools/oracle/dosfs.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/files.c\
	sys/cdimage.c\
	sys/compat.c\
	lib386/lib_sys/expand.c\

MIXSRC=\
	tools/oracle/mixoracle.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/sound.c\
	sys/compat.c\
	lib386/lib_sys/expand.c\

AILSRC=\
	tools/oracle/ailoracle.c\
	tools/oracle/dosfs.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/midi.c\
	sys/xmidi.c\
	sys/yamaha.c\
	sys/oplio.c\
	lib386/lib_sys/expand.c\

MEMSRC=\
	tools/oracle/memoracle.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/mem.c\
	lib386/lib_sys/expand.c\

VGASRC=\
	tools/oracle/vgaoracle.c\
	tools/oracle/pc.c\
	tools/oracle/dll.c\
	tools/oracle/abi.c\
	sys/video.c\
	sys/timer.c\
	sources/mcga.c\
	lib386/lib_sys/expand.c\

ASMSRC=\
	tools/oracle/asmoracle.c\
	tools/oracle/abi.c\
	tools/oracle/afunc.c\
	tools/oracle/atrigo.c\
	tools/oracle/aanim.c\
	tools/oracle/asvga.c\
	tools/oracle/agrille.c\
	sources/grille_a.c\
	tools/oracle/amisc.c\
	sources/balance.c\
	lib386/lib_sys/hqr.c\
	sources/adfli_a.c\
	sources/cpymask.c\
	sources/fire.c\
	sources/func.c\
	sources/mcga.c\
	lib386/lib_3d/p_func.c\
	lib386/lib_3d/p_trigo.c\
	lib386/lib_3d/p_anim.c\
	lib386/lib_3d/p_ob_iso.c\
	lib386/lib_3d/p_sintab.c\
	lib386/lib_sys/expand.c\
	lib386/lib_sys/buffer_a.c\
	lib386/lib_svga/s_line.c\
	lib386/lib_svga/s_box.c\
	lib386/lib_svga/s_block.c\
	lib386/lib_svga/s_screen.c\
	lib386/lib_svga/graph_a.c\
	lib386/lib_svga/graphmsk.c\
	lib386/lib_svga/mask_a.c\
	lib386/lib_svga/font_a.c\
	lib386/lib_svga/s_poly.c\
	lib386/lib_svga/s_fillv.c\
	lib386/lib_svga/texture.c\

LBA2SRC=\
	tools/oracle/lba2oracle.c\
	lib386/lib_sys/lzss.c\
	lib386/lib_sys/compress.c\
	sources/func.c\
	lib386/lib_sys/expand.c\

# the headers the oracles read, the game's, the port's and their own,
# so that a change of one builds every oracle again
ORACLEHDR=$(wildcard sources/*.h sources/*.def sys/*.h sys/nuked/*.h lib386/*/*.h tools/oracle/*.h)
LBA2=$(abspath ../lba2-classic/SOURCES)
# jwasm, which make fetch installs, reads options from JWASM
unexport JWASM

# oracles builds the oracles, lba2oracle's own side but its side of
# LBA2's.
oracles: $(ORACLE)/oracle $(ORACLE)/fmoracle $(ORACLE)/aweoracle $(ORACLE)/waveoracle\
	$(ORACLE)/cdoracle $(ORACLE)/keyoracle $(ORACLE)/timeoracle $(ORACLE)/vgaoracle\
	$(ORACLE)/fileoracle $(ORACLE)/mixoracle $(ORACLE)/memoracle $(ORACLE)/ailoracle\
	$(ORACLE)/asmoracle $(ORACLE)/lba2/ours.o

# asan builds asmoracle-asan.
asan: $(ORACLE)/asmoracle-asan

$(ORACLE)/oracle: $(ORACLESRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLELDFLAGS) -o $@ $(ORACLESRC)

$(ORACLE)/fmoracle: $(FMSRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(FMSRC)

$(ORACLE)/aweoracle: $(AWESRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(AWESRC)

$(ORACLE)/waveoracle: $(WAVESRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(WAVESRC)

$(ORACLE)/cdoracle: $(CDSRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(CDSRC)

$(ORACLE)/keyoracle: $(KEYSRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(KEYSRC)

$(ORACLE)/timeoracle: $(TIMESRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(TIMESRC)

$(ORACLE)/vgaoracle: $(VGASRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(VGASRC)

$(ORACLE)/fileoracle: $(FILESRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(FILESRC)

$(ORACLE)/mixoracle: $(MIXSRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(MIXSRC) -lm

$(ORACLE)/memoracle: $(MEMSRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(MEMSRC)

$(ORACLE)/ailoracle: $(AILSRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ORACLELDFLAGS) -o $@ $(AILSRC)

# asmoracle's objects apart, so that a change of one file, a mutant's,
# compiles that file alone
ASMOBJ=$(ASMSRC:%.c=$(ORACLE)/asm/%.o)

$(ORACLE)/asm/%.o: %.c
	mkdir -p $(@D)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) -MMD -MP -c -o $@ $<

$(ORACLE)/asmoracle: $(ASMOBJ)
	$(ORACLECC) $(ORACLELDFLAGS) -o $@ $(ASMOBJ) -lm

# asmoracle under AddressSanitizer, its objects apart: a read or write
# of a translation past an array, which no test reads back, fails it
# (tools/mutate.py's second test). The fixes case reads beside
# TabVerticG on purpose, which it reports.
ASANFLAGS=-fsanitize=address -fno-omit-frame-pointer
ASANOBJ=$(ASMSRC:%.c=$(ORACLE)/asan/%.o)

$(ORACLE)/asan/%.o: %.c
	mkdir -p $(@D)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) $(ASANFLAGS) -MMD -MP -c -o $@ $<

$(ORACLE)/asmoracle-asan: $(ASANOBJ)
	$(ORACLECC) $(ORACLELDFLAGS) $(ASANFLAGS) -o $@ $(ASANOBJ) -lm

# lba2oracle's own side, ours and the oracle, linked into one object
$(ORACLE)/lba2/ours.o: $(LBA2SRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)/lba2
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLEPORTFLAGS) -r -nostdlib -o $@ $(LBA2SRC)

# LBA2's side, its symbols prefixed lba2_: LZSS.CPP as C, over a
# c_extern.h that gives it LZSS.H alone, COMPRESS.ASM in the flat model
# with MASM 5.1's structures, FUNC.ASM as it is
$(ORACLE)/lba2/lba2.o: $(LBA2)/LZSS.CPP $(LBA2)/LZSS.H $(LBA2)/COMPRESS.ASM $(LBA2)/FUNC.ASM $(JWASM_OK)
	mkdir -p $(ORACLE)/lba2
	printf '#include "LZSS.H"\n' >$(ORACLE)/lba2/c_extern.h
	$(ORACLECC) -m32 -w -O2 -fno-strict-aliasing -I$(ORACLE)/lba2 -I$(LBA2) -x c -c -o $(ORACLE)/lba2/lzss.o $(LBA2)/LZSS.CPP
	sed -e 's/\.MODEL\tSMALL,SYSCALL/.MODEL\tFLAT,SYSCALL/'\
		-e '0,/^\t\t\t\.386$$/s//&\n\t\t\tOPTION\tOLDSTRUCTS/' $(LBA2)/COMPRESS.ASM >$(ORACLE)/lba2/compress.asm
	$(JWASMEXE) -q -Cx -Zg -W0 -Djumps= -elf -Fo$(ORACLE)/lba2/compress.o $(ORACLE)/lba2/compress.asm
	$(JWASMEXE) -q -Cx -Zg -W0 -Djumps= -elf -Fo$(ORACLE)/lba2/func.o $(LBA2)/FUNC.ASM
	ld -m elf_i386 -r -o $(ORACLE)/lba2/both.o $(ORACLE)/lba2/lzss.o $(ORACLE)/lba2/compress.o $(ORACLE)/lba2/func.o
	objcopy --prefix-symbols=lba2_ $(ORACLE)/lba2/both.o $@

# JWasm's objects lack .note.GNU-stack: the stack kept unexecutable
$(ORACLE)/lba2oracle: $(ORACLE)/lba2/ours.o $(ORACLE)/lba2/lba2.o
	$(ORACLECC) $(ORACLELDFLAGS) -Wl,-z,noexecstack -o $@ $(ORACLE)/lba2/ours.o $(ORACLE)/lba2/lba2.o

# oraclecheck runs the oracles on the game's files in LBATMP: LBA.EXE's
# image and the CD's drivers as tools/extract writes them.
oraclecheck: $(ORACLE)/oracle $(ORACLE)/fmoracle $(ORACLE)/aweoracle $(ORACLE)/waveoracle\
	$(ORACLE)/lba2oracle
	$(ORACLE)/oracle $(LBATMP)/elf/lba.elf re/lba.exe.sym
	$(ORACLE)/fmoracle $(LBATMP)/elf/dll/a32sp2fm.elf $(LBATMP)/pc/LBA/MIDI_SB.HQR $(LBATMP)/pc/LBA/SAMPLE.OPL
	$(ORACLE)/aweoracle $(LBATMP)/elf/dll/a32gfx.elf $(LBATMP)/pc/LBA/MIDI_MI.HQR $(LBATMP)/pc/LBA/MIDI_SB.HQR -
	$(ORACLE)/waveoracle -g $(LBATMP)/pc/LBA $(LBATMP)/elf/dll/w_sb16.elf re/w_sb16.dll.sym
	$(ORACLE)/lba2oracle -g $(LBATMP)/pc/LBA

# emu8kdiff: our EMU8000 against 86Box's, built from its sources in
# $(LBATMP)/downloads/86box (commit 9ce8877f) with
# tools/emu8kdiff/inc/86box's stand-ins for the headers it includes.
EMU8KSRC=$(LBATMP)/downloads/86box

$(ORACLE)/emu8kdiff/inc/86box/snd_emu8k.h: $(EMU8KSRC)/snd_emu8k.h $(wildcard tools/emu8kdiff/inc/86box/*.h)
	mkdir -p $(@D)
	cp tools/emu8kdiff/inc/86box/*.h $(EMU8KSRC)/snd_emu8k.h $(@D)/

$(ORACLE)/emu8kdiff/emu8kdiff: tools/emu8kdiff/emu8kdiff.c $(EMU8KSRC)/snd_emu8k.c $(ORACLE)/emu8kdiff/inc/86box/snd_emu8k.h
	$(ORACLECC) -std=gnu11 -O2 -g -w -I$(ORACLE)/emu8kdiff/inc -o $@ tools/emu8kdiff/emu8kdiff.c $(EMU8KSRC)/snd_emu8k.c -lm

-include $(ASMOBJ:.o=.d) $(ASANOBJ:.o=.d)
