# watcom.mk builds the 1994 game for DOS. LBA.EXE's code is that of
# Watcom C 9.01d, but lib_midi's of Watcom C 9.5, and of MASM 6.11 but
# cdrom.asm's of Turbo Assembler 3.1, and the CD's wave drivers come from
# Watcom C/C++ 10.0a's wlink. CC=openwatcom builds it with OpenWatcom 2
# ($(WATCOM)) and JWasm ($(DOSAS)), CC=watcom with those five in DOSBox
# (dos/watcom, $(W901), $(W95), $(MASM), $(TASM), $(W100A)), each
# tool where make fetch puts it (mk/fetch.mk). CC=watcom builds LBA.EXE's
# code, and with FIXES=1 the fixes of its faults, which the code holds
# under #ifdef FIXES (IFDEF FIXES in the assembly). CC=openwatcom takes
# them always, as OpenWatcom's code needs two of them to run. The C takes
# Watcom's /oeaxt /zp2 /4s /s with WATCOM9, CDROM and DOS defined, and
# dos/compat.h read first. The assembly takes the 1994 makefiles' flags,
# /Cx /Zm and NoLanguage=SYSCALL, lib_cd.mak's for cdrom.asm, JWasm's -Zg
# laying out frames as MASM did, and AIL/32 is assembled as a32wr.mak
# does. A module whose .asm is in the tree is built from it, as LBA.EXE
# linked the assembly, any other from its .c. The game's objects link in
# LBA.EXE's order and the libraries' members in the 1994 makefiles',
# s_mouse aside, which the game never reaches, so that the members it
# reaches are the ones linked. The program is lba0.exe, the 1994
# makefile's own name, with its map, lba0.sym and lba0.elf
# (tools/wmap2sym.py, tools/le2elf.py), and lba0.sums, the SHA-256 of
# the exe and of the wave drivers, then each symbol's sum
# (tools/lecmp.py -s), which make test compares with dos/$(CC).sums, or
# with FIXES=1 dos/watcom-fixes.sums. Each driver comes with its map,
# its .sym and its .elf, which tools/lecmp.py compares with the CD's.

DOSAS=$(JWASMEXE)
# the variables wcc386, jwasm and wlink read options or paths from
unexport WCC386 JWASM WLINK_LNK LIB INCLUDE

# FIXES where the build takes the fixes
DOSFIXES=$(if $(or $(FIXES),$(filter openwatcom,$(CC))),FIXES)
DOSASFLAGS=-c -q -X -Cx -Zg -W0 -Sa -DNoLanguage=SYSCALL -Djumps= $(DOSFIXES:%=-D%)

DOSGAME=$(addprefix sources/,\
	version perso object global flipbox diskfunc grille_a fiche extra\
	incrust grille func cpymask balance message ambiance fire gamemenu\
	geretrak gerelife holomap adfli_a playfla mcga $(if $(DOSFIXES),gridfix textfix))
DOSSYS=$(addprefix lib386/lib_sys/,\
	files malloc loadmall loadsave def_file system timer_a timer buffer_a\
	divers keyb keyboard expand hq_ress hqr dllload adeline hq_mem hq_r_m\
	files_a)
DOSSVGA=$(addprefix lib386/lib_svga/,\
	initmode initsvga graph_a s_text s_string mask_a font_a s_screen\
	s_block s_block2 s_pal s_plot s_box s_line s_poly s_fillv s_mousea\
	rect zoom maskgph graphmsk s_phys texture s_block3 lbm pcx s_dll vesa\
	gif)
DOS3D=$(addprefix lib386/lib_3d/,p_sintab p_anim p_ob_iso p_trigo p_func)
DOSMIX=lib386/lib_mix/mixer lib386/lib_mix/mixer_i
DOSWAVE=lib386/lib_samp/wave lib386/lib_samp/wave_i
DOSMIDI=lib386/lib_midi/midi lib386/lib_midi/ail32/ail32
DOSCD=lib386/lib_cd/cdrom

DOSMOD=$(DOSGAME) $(DOSSYS) $(DOSSVGA) $(DOS3D) $(DOSMIX) $(DOSWAVE)\
	$(DOSMIDI) $(DOSCD)
DOSASM=$(foreach m,$(DOSMOD),$(if $(wildcard $(m).asm),$(m)))
DOSC=$(filter-out $(DOSASM),$(DOSMOD))

# The assemblers: OpenWatcom's build assembles every module with JWasm,
# and Watcom C 9's assembles them as LBA.EXE's were, in dos/watcom's
# DOSBox: cdrom.asm (DOSTASM) with Turbo Assembler 3.1, the others
# (DOSML) with MASM 6.11.
DOSTASM=lib386/lib_cd/cdrom
DOSML=$(filter-out $(DOSTASM),$(DOSASM))
DOSINC=$(wildcard lib386/*/*.ash lib386/*/*/*.inc lib386/*/*/*.mac)

# The wave drivers, made from lib_samp/wave_a.asm as its wave_dll.mak
# makes them: those the CD ships but W_GUS.DLL, which links Gravis' SDK,
# and the three more of the North American CD, each an OS/2 LX DLL in
# $(O), where tools/dosrun takes them in place of the CD's.
DOSDLL=w_sb w_sb2 w_sbpro w_sb16 w_sb16_8 w_sb16_m w_sb16_s w_pas w_pas16\
	w_mwss w_gold
w_sb_DEFS=-DSBLASTER1
w_sb2_DEFS=-DSBLASTER
w_sbpro_DEFS=-DSBPRO
w_sb16_DEFS=-DSB16 -DSTEREO -DSAMPLE16BIT
w_sb16_8_DEFS=-DSB16 -DSTEREO
w_sb16_m_DEFS=-DSB16 -DSAMPLE16BIT
w_sb16_s_DEFS=-DSB16 -DSTEREO -DSAMPLE16BIT -DSURROUND
w_pas_DEFS=-DPAS
w_pas16_DEFS=-DPAS16
w_mwss_DEFS=-DMWSS
w_gold_DEFS=-DGOLD

# DOSQSORT is OpenWatcom's copy of Watcom C 9.01's qsort, and DOS9SRC
# the sources Watcom C 9 compiles, from a copy in $(O)/src.
DOSQSORT=sys/qsort
DOS9SRC=$(DOSC:%=%.c) dos/compat.h\
	$(wildcard sources/*.h sources/*.def lib386/*/*.h lib386/*/*/*.h)

# The directories of the objects, made before any of them.
DIRS_openwatcom=$(DOSMOD:%=$(O)/%.obj) $(DOSQSORT:%=$(O)/%.obj) $(O)/ail32/ail32.h
DIRS_watcom=$(DOSASM:%=$(O)/%.obj) $(DOS9SRC:%=$(O)/src/%)
DIRS=$(patsubst %/,%,$(sort $(dir $(DIRS_$(CC)) $(DOSDLL:%=$(O)/lib386/lib_samp/%.obj))))

# all builds the program, the wave drivers, and the sums make test
# compares.
all: $(O)/lba0.exe $(DOSDLL:%=$(O)/%.dll) $(O)/lba0.sums $(DOSDLL:%=$(O)/%.elf)

# The distribution holds the program, the wave drivers, the tree's
# licence and AIL/32's. The program comes last, its file the folder's.
$(D)/lba0.exe: $(O)/lba0.exe $(DOSDLL:%=$(O)/%.dll) LICENSE lib386/lib_midi/ail32/license
	rm -rf $(D) && mkdir -p $(D)
	cp $(DOSDLL:%=$(O)/%.dll) LICENSE $(D)/
	cp lib386/lib_midi/ail32/license $(D)/LICENSE.ail32
	cp $(O)/lba0.exe $@

# tools/pack.py writes the folder as an archive whose files take DATE,
# so that the same folder gives the same bytes.
$(ARCHIVE): $(D)/lba0.exe tools/pack.py
	python3 -I tools/pack.py -t $(DATE) $(D) $@

# dist builds the distribution and its archive.
dist: $(D)/lba0.exe $(ARCHIVE)

# Watcom C/C++ 10.0a's qsort, the module of its register-call DOS library
# that LBA.EXE links first, dead code at 0x10010: both builds link it
# first, from the library make fetch takes from 10.0a's CD
$(O)/qsort10.obj: $(W100A_OK) tools/libmod.py | $(DIRS)
	python3 -I tools/libmod.py $(W100A)/WATCOM/LIB386/DOS/CLIB3R.LIB qsort >$@

ifeq ($(CC),openwatcom)
# OpenWatcom links Watcom C 9.01's qsort (sys/qsort.c), in a library of
# its own, in place of its run-time's, and reads ail32.h from a copy less
# its last byte, a Ctrl-Z, which it rejects. It writes a rule for each
# object alone, so that a header renamed stops the build until
# build/dos-386-openwatcom is removed.
WATCOM=$(OW2)
export WATCOM
export PATH:=$(WATCOM)/binl64:$(PATH)
DOS4GW=$(WATCOM)/binw/dos4gw.exe

$(DOSASM:%=$(O)/%.obj): $(O)/%.obj: %.asm $(DOSINC) $(JWASM_OK) | $(DIRS)
	$(DOSAS) $(DOSASFLAGS) -Fo=$@ $<

# cdrom.asm was TASM's: its PUBLIC NoLanguage is -zcw, its frames
# dos/tasm.inc's
$(O)/lib386/lib_cd/cdrom.obj: DOSASFLAGS=-c -q -X -Cx -Zg -W0 -Sa -DNoLanguage=C -zcw -Djumps= -Fidos/tasm.inc $(DOSFIXES:%=-D%)
$(O)/lib386/lib_cd/cdrom.obj: dos/tasm.inc
# AIL/32 as a32wr.mak assembles it
$(O)/lib386/lib_midi/ail32/ail32.obj: DOSASFLAGS=-c -q -X -W0 -Cp -Zd -DDPMI

DOSCFLAGS=-bt=dos -oeaxt -zp2 -4s -zq -s -w0 -x -i=$(WATCOM)/h -i=$(O)/ail32\
	-dWATCOM9 -dCDROM=1 -dDOS -dFIXES -fi=dos/compat.h
DOSLIBS=$(addprefix $(O)/,lib_sys.lib lib_svga.lib lib_3d.lib lib_mix.lib\
	lib_wave.lib lib_midi.lib lib_cd.lib qsort.lib)
$(DOSC:%=$(O)/%.obj) $(DOSQSORT:%=$(O)/%.obj): $(O)/%.obj: %.c dos/compat.h $(OW2_OK) | $(DIRS)
	wcc386 $(DOSCFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<

# ail32.h as vendored, less the Ctrl-Z that ends it
$(O)/ail32/ail32.h: lib386/lib_midi/ail32/ail32.h | $(DIRS)
	tr -d '\032' <$< >$@
$(O)/lib386/lib_midi/midi.obj $(O)/lib386/lib_sys/adeline.obj: $(O)/ail32/ail32.h

# one wlib at a time: two in one directory clash over their work file
MKLIB=rm -f $@ && wlib -q -b -n -c $@ $(addprefix +,$^)
$(O)/lib_sys.lib: $(DOSSYS:%=$(O)/%.obj); $(MKLIB)
$(O)/lib_svga.lib: $(DOSSVGA:%=$(O)/%.obj) | $(O)/lib_sys.lib; $(MKLIB)
$(O)/lib_3d.lib: $(DOS3D:%=$(O)/%.obj) | $(O)/lib_svga.lib; $(MKLIB)
$(O)/lib_mix.lib: $(DOSMIX:%=$(O)/%.obj) | $(O)/lib_3d.lib; $(MKLIB)
$(O)/lib_wave.lib: $(DOSWAVE:%=$(O)/%.obj) | $(O)/lib_mix.lib; $(MKLIB)
$(O)/lib_midi.lib: $(DOSMIDI:%=$(O)/%.obj) | $(O)/lib_wave.lib; $(MKLIB)
$(O)/lib_cd.lib: $(DOSCD:%=$(O)/%.obj) | $(O)/lib_midi.lib; $(MKLIB)
$(O)/qsort.lib: $(DOSQSORT:%=$(O)/%.obj) | $(O)/lib_cd.lib; $(MKLIB)

# lba0.map comes with lba0.exe, its statics named
$(O)/lba0.exe: $(O)/qsort10.obj $(DOSGAME:%=$(O)/%.obj) $(DOSLIBS)
	wlink system dos4g name $@ option quiet, caseexact, statics, stack=7000, map=$(O)/lba0.map\
		file { $(O)/qsort10.obj $(DOSGAME:%=$(O)/%.obj) } library { $(DOSLIBS) }

$(DOSDLL:%=$(O)/lib386/lib_samp/%.obj): $(O)/lib386/lib_samp/%.obj: lib386/lib_samp/wave_a.asm lib386/lib_samp/wave.inc $(JWASM_OK) | $(DIRS)
	$(DOSAS) $(DOSASFLAGS) $($*_DEFS) -Fo=$@ $<

$(DOSDLL:%=$(O)/%.dll): $(O)/%.dll: $(O)/lib386/lib_samp/%.obj $(OW2_OK)
	wlink name $@ file $< format os2 lx dll option quiet, map=$(@:.dll=.map)

-include $(wildcard $(O)/*/*.d $(O)/*/*/*.d $(O)/*/*/*/*.d)
else
# MASM 6.11 assembles DOSML and TASM 3.1 DOSTASM from the tree, Watcom C
# 9.01 compiles the C, and 9.5 lib_midi's (DOS95), as their 1994
# makefiles say, the C from a copy of the sources in $(O)/src less the
# push and pop of #pragma pack, which 9.01 rejects (-zp2 lays every
# structure out alike). One DOSBox assembles and compiles every module,
# makes the libraries and links again when any source changes (dos/watcom
# build). The run-time's own qsort is LBA.EXE's.
DOS4GW=$(W901)/WATCOM/BIN/DOS4GW.EXE
DOS95=lib386/lib_midi/midi
DOSDEFS=$(DOSFIXES:%=-d%)
DOSMLDEFS=$(DOSFIXES:%=/D%)
DOSTASMDEFS=$(DOSFIXES:%=/d%)
export DOSGAME DOSSYS DOSSVGA DOS3D DOSMIX DOSWAVE DOSMIDI DOSCD DOSASM DOSML DOSTASM DOS95 DOSDLL
export W901 W95 W100A MASM TASM DOSDEFS DOSMLDEFS DOSTASMDEFS
export $(DOSDLL:%=%_DEFS)
$(O)/src/%: % | $(DIRS)
	LC_ALL=C sed '/^#pragma pack(\(push\|pop\)/d' $< >$@

# lba0.map comes with lba0.exe
$(O)/lba0.exe: $(DOS9SRC:%=$(O)/src/%) $(DOSASM:%=%.asm) $(DOSINC) $(O)/qsort10.obj dos/watcom\
	$(W901_OK) $(W95_OK) $(ML_OK) $(TASM_OK)
	dos/watcom build $(O)

# the wave drivers, assembled by ML 6.11 and linked by 10.0a's wlink, as
# the CD's were, in a DOSBox of their own
$(DOSDLL:%=$(O)/%.dll) &: lib386/lib_samp/wave_a.asm lib386/lib_samp/wave.inc dos/watcom $(W100A_OK) $(ML_OK) | $(DIRS)
	dos/watcom dll $(O)
endif

$(DIRS):
	mkdir -p $@

$(O)/lba0.sym: $(O)/lba0.exe tools/wmap2sym.py
	python3 -I tools/wmap2sym.py $(O)/lba0.map $(O)/lba0.exe >$@

# lba0.fix comes with lba0.elf
$(O)/lba0.elf: $(O)/lba0.sym tools/le2elf.py
	python3 -I tools/le2elf.py -s $(O)/lba0.sym -f $(O)/lba0.fix -o $@ $(O)/lba0.exe >/dev/null

$(O)/lba0.sums: $(O)/lba0.elf $(DOSDLL:%=$(O)/%.dll) tools/lecmp.py
	cd $(O) && sha256sum lba0.exe $(DOSDLL:%=%.dll) >lba0.sums
	python3 -I tools/lecmp.py -s $(O)/lba0.elf $(O)/lba0.sym >>$@

# each driver's map comes with it
$(DOSDLL:%=$(O)/%.sym): $(O)/%.sym: $(O)/%.dll tools/wmap2sym.py
	python3 -I tools/wmap2sym.py $(O)/$*.map $< >$@

$(DOSDLL:%=$(O)/%.elf): $(O)/%.elf: $(O)/%.sym tools/le2elf.py
	python3 -I tools/le2elf.py -s $< -f $(O)/$*.fix -o $@ $(O)/$*.dll >/dev/null

ifneq ($(DOSFIXES),)
# test, with the fixes: the build's sums against the reference, the
# oracle on its image (its rand, srand and qsort against ours,
# Watcom C 9's being LBA.EXE's), asmoracle on it (its assembly against
# the translations into C, the fixes where LBA.EXE's faults among them),
# and its run to No CD.
test: all oracles
	diff dos/$(CC)$(if $(FIXES),-fixes).sums $(O)/lba0.sums
	$(ORACLE)/oracle $(O)/lba0.elf $(O)/lba0.sym
	$(ORACLE)/asmoracle $(O)/lba0.elf $(O)/lba0.sym
	$(ORACLE)/asmoracle $(O)/lba0.elf $(O)/lba0.sym fixes
	dos/nodata -g $(DOS4GW) -x $(O)/lba0.exe

# check, with the fixes: lint and test, then asmoracle on the game's
# files.
check: lint test
	$(ORACLE)/asmoracle -g $(LBATMP)/pc/LBA $(O)/lba0.elf $(O)/lba0.sym
else
# test, LBA.EXE's code: the build's sums, and the oracle whose cases
# hold it as they hold the fixes, the run-time's, then its run to No CD.
test: all oracles
	diff dos/$(CC).sums $(O)/lba0.sums
	$(ORACLE)/oracle $(O)/lba0.elf $(O)/lba0.sym
	dos/nodata -g $(DOS4GW) -x $(O)/lba0.exe

# check, LBA.EXE's code: lint and test.
check: lint test
endif
