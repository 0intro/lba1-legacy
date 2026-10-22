# port.mk builds the game on SDL3 with gcc or clang, for the target that
# OS and ARCH name: GAME and LIBS with sys/compat.h read first, the rest
# with every warning an error. Each Windows program links
# sys/lba.manifest, through sys/lba.rc, which makes its code page UTF-8,
# the code page of the paths SDL gives.

# The compiler's command. gcc and clang take the architecture as a flag
# on Linux, and mingw-w64 names it in the command, which CROSS= leaves
# to the shell's own tools on Windows itself (MSYS2's UCRT64).
MINGW_386=i686-w64-mingw32
MINGW_amd64=x86_64-w64-mingw32
CROSS_windows=$(MINGW_$(ARCH))-
CROSS=$(CROSS_$(OS))
M_linux_386=-m32
SANITIZE_ubsan=-fsanitize=undefined -fno-sanitize-recover=undefined
SANITIZE_asan=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
COMPILE=$(CROSS)$(CC) $(M_$(OS)_$(ARCH)) $(SANITIZE_$(SANITIZE))

# pkg-config finds SDL3: a Linux target the system's, the 32-bit one for
# 386 on a 64-bit Linux, and a Windows target the one make fetch unpacks
# into SDLDIR, whose stamp the objects that read its headers name, and
# SDL3.dll.
PKG_CONFIG_linux_386=PKG_CONFIG_LIBDIR=/usr/lib/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig pkg-config
PKG_CONFIG_linux_amd64=pkg-config
PKG_CONFIG_windows_386=PKG_CONFIG_LIBDIR=$(SDLDIR)/$(MINGW_386)/lib/pkgconfig pkg-config
PKG_CONFIG_windows_amd64=PKG_CONFIG_LIBDIR=$(SDLDIR)/$(MINGW_amd64)/lib/pkgconfig pkg-config
PKG_CONFIG=$(PKG_CONFIG_$(OS)_$(ARCH))
SDLDEP_windows=$(SDL_OK)
SDLCFLAGS=$(shell $(PKG_CONFIG) --cflags sdl3)
SDLLIBS=$(shell $(PKG_CONFIG) --libs sdl3)

# The flags. Every compile writes the headers it read beside its object
# (-MMD) and names its files in its debug tables from the tree's root,
# and LBATMP's, SDL3's for Windows among them, as ../lba1-tmp
# (-ffile-prefix-map), so that a build's bytes are the same wherever the
# two lie. gcc takes the last map whose path starts a file's, so the
# maps go in their paths' order: of two paths where one starts the
# other, the longer last. OLDFLAGS are GAME's and LIBS', NEWFLAGS the
# others', and LD a program's link: on Windows without the link's time
# in its header.
MAPS=$(foreach p,$(sort $(CURDIR) $(LBATMP)),\
	-ffile-prefix-map=$(p)=$(if $(filter $(CURDIR),$(p)),.,../lba1-tmp))
CFLAGS=-std=c99 -pedantic -funsigned-char -fno-strict-aliasing -fwrapv -O2 -g\
	$(MAPS) -MMD -MP -DCDROM=1
OLDFLAGS=-w -include sys/compat.h
DEFS_linux=-D_DEFAULT_SOURCE
NEWFLAGS=-Wall -Wextra -Werror $(DEFS_$(OS))
LD_windows=-Wl,--no-insert-timestamp
LD=$(LD_$(OS))

# A Windows program ends in .exe and links sys/lba.rc's resource, lba.exe
# as a windowed program (-mwindows), and SDL3.dll lies beside them.
EXE_windows=.exe
EXE=$(EXE_$(OS))
GUI_windows=-mwindows
RES_windows=$(O)/sys/lba.res.o
MANIFEST=$(RES_$(OS))
DLL_windows=$(O)/SDL3.dll

# RUN starts a Windows program under Wine, in the project's prefix and
# Xephyr (tools/winerun), and HOSTPATH gives it a path as Wine names it.
RUN_windows=tools/winerun -q
RUN=$(RUN_$(OS))
HOSTPATH_linux=$(1)
HOSTPATH_windows='Z:$(subst /,\,$(abspath $(1)))'
HOSTPATH=$(HOSTPATH_$(OS))

# TESTS lists the unit tests of every target, TESTS_linux those of Linux
# alone: padtest drives SDL's virtual gamepad.
TESTS=\
	emu8ktest\
	cfgtest\
	inputtest\
	syskeystest\
	widetest\
	clocktest\
	filestest\
	cdimagetest\

TESTS_linux=\
	padtest\

# The objects, the game's in their link order, and their directories.
OFILES=$(addprefix $(O)/,$(GAME) $(LIBS) $(NEW) $(CARRIED) $(PORT) $(OPL3) $(DECODE) $(ARRAYS))
NEWOFILES=$(addprefix $(O)/,$(NEW) $(CARRIED) $(TESTS:%=test/%.o) tools/pcm.o)
DIRS=$(patsubst %/,%,$(sort $(dir $(OFILES) $(NEWOFILES) $(O)/test/padtest.o)))

# all builds the game, lba.cfg beside it, the tool and the unit tests.
all: $(O)/lba$(EXE) $(O)/lba.cfg $(O)/pcm$(EXE) $(DLL_$(OS))\
	$(TESTS:%=$(O)/%$(EXE)) $(TESTS_$(OS):%=$(O)/%$(EXE))

# The distribution holds the game and lba.cfg, the tree's licence and
# those of the code it carries, and on Windows SDL3.dll and its licence.
# The game comes last, its file the folder's.
SDLLICENCE_windows=$(SDLDIR)/$(MINGW_$(ARCH))/share/licenses/SDL3/LICENSE.txt
$(D)/lba$(EXE): $(O)/lba$(EXE) $(O)/lba.cfg $(DLL_$(OS)) $(SDLDEP_$(OS)) LICENSE\
	sys/nuked/LICENSE sys/drlibs/LICENSE sys/stb/LICENSE lib386/lib_midi/ail32/license
	rm -rf $(D) && mkdir -p $(D)
	cp $(O)/lba.cfg $(DLL_$(OS)) LICENSE $(D)/
	cp sys/nuked/LICENSE $(D)/LICENSE.nuked
	cp sys/drlibs/LICENSE $(D)/LICENSE.drlibs
	cp sys/stb/LICENSE $(D)/LICENSE.stb
	cp lib386/lib_midi/ail32/license $(D)/LICENSE.ail32
	$(if $(SDLLICENCE_$(OS)),cp $(SDLLICENCE_$(OS)) $(D)/LICENSE.SDL3)
	cp $(O)/lba$(EXE) $@

# tools/pack.py writes the folder as an archive whose files take DATE,
# so that the same folder gives the same bytes.
$(ARCHIVE): $(D)/lba$(EXE) tools/pack.py
	python3 -I tools/pack.py -t $(DATE) $(D) $@

# dist builds the distribution and its archive.
dist: $(D)/lba$(EXE) $(ARCHIVE)

$(DIRS):
	mkdir -p $@

# The game links SDL3, and on Windows its resources.
$(O)/lba$(EXE): $(OFILES) $(RES_$(OS))
	$(COMPILE) $(GUI_$(OS)) $(LD) -o $@ $^ $(SDLLIBS) -lm

# sys/lba.cfg beside the program, which a run reads after the
# preferences and the game's directories.
$(O)/lba.cfg: sys/lba.cfg | $(DIRS)
	cp $< $@

# SDL3.dll beside a Windows program.
$(O)/SDL3.dll: $(SDL_OK) | $(DIRS)
	cp $(SDLDIR)/$(MINGW_$(ARCH))/bin/SDL3.dll $@

# sys/lba.cfg's bytes as a C array, lbacfg (sys/dat.h), a 0 after them.
$(O)/sys/lbacfg.c: sys/lba.cfg | $(DIRS)
	od -An -v -tx1 $< >$@.od
	{ echo '#include <stdint.h>' &&\
		echo 'uint8_t $(basename $(@F))[] = {' &&\
		sed 's/[0-9a-f][0-9a-f]/0x&,/g' $@.od &&\
		echo '0x00};' &&\
		echo 'uint32_t n$(basename $(@F)) = sizeof $(basename $(@F)) - 1;'; } >$@
	rm -f $@.od

# The Windows programs' resource, sys/lba.rc: the manifest.
$(O)/sys/%.res.o: sys/%.rc sys/lba.manifest | $(DIRS)
	$(CROSS)windres -I sys -O coff -o $@ $<

# The console programs: the unit tests and the tool.
LINK=$(COMPILE) $(LD) -o $@ $^

$(O)/emu8ktest$(EXE): $(addprefix $(O)/,$(EMU8KTEST)) $(MANIFEST)
	$(LINK) -lm

$(O)/cfgtest$(EXE): $(addprefix $(O)/,$(CFGTEST) $(DECODE)) $(MANIFEST)
	$(LINK) -lm

$(O)/inputtest$(EXE): $(addprefix $(O)/,$(INPUTTEST) $(DECODE)) $(MANIFEST)
	$(LINK) -lm

$(O)/syskeystest$(EXE): $(addprefix $(O)/,$(SYSKEYSTEST)) $(MANIFEST)
	$(LINK)

$(O)/widetest$(EXE): $(addprefix $(O)/,$(WIDETEST)) $(MANIFEST)
	$(LINK)

$(O)/clocktest$(EXE): $(addprefix $(O)/,$(CLOCKTEST)) $(MANIFEST)
	$(LINK)

$(O)/filestest$(EXE): $(addprefix $(O)/,$(FILESTEST) $(DECODE)) $(MANIFEST)
	$(LINK) -lm

$(O)/cdimagetest$(EXE): $(addprefix $(O)/,$(CDIMAGETEST) $(DECODE)) $(MANIFEST)
	$(LINK) -lm

$(O)/padtest$(EXE): $(addprefix $(O)/,$(PADTEST)) $(MANIFEST)
	$(LINK) $(SDLLIBS) -lm

$(O)/pcm$(EXE): $(addprefix $(O)/,$(PCM) $(DECODE)) $(MANIFEST)
	$(LINK) -lm

# livemusic.so: test/livemusic's preload, which watches the music's
# stream in play and stalls the game, on Linux alone, as Wine's builds
# refuse a preload.
$(O)/livemusic.so: test/livemusic.c | $(DIRS)
	$(COMPILE) $(CFLAGS) $(NEWFLAGS) $(SDLCFLAGS) -fPIC -shared -o $@ $< $(SDLLIBS) -ldl

# GAME and LIBS, the 1994 code.
$(addprefix $(O)/,$(GAME) $(LIBS)): $(O)/%.o: %.c | $(DIRS)
	$(COMPILE) $(CFLAGS) $(OLDFLAGS) -c -o $@ $<

# NEW, CARRIED, the unit tests and the tool.
$(NEWOFILES): $(O)/%.o: %.c | $(DIRS)
	$(COMPILE) $(CFLAGS) $(NEWFLAGS) -c -o $@ $<

# sys/ and padtest, which read SDL's headers.
$(addprefix $(O)/,$(PORT) test/padtest.o): $(O)/%.o: %.c $(SDLDEP_$(OS)) | $(DIRS)
	$(COMPILE) $(CFLAGS) $(NEWFLAGS) $(SDLCFLAGS) -c -o $@ $<

# Nuked OPL3 and stb_vorbis as published.
$(addprefix $(O)/,$(OPL3) sys/stb/stb_vorbis.o): $(O)/%.o: %.c | $(DIRS)
	$(COMPILE) $(CFLAGS) -w -c -o $@ $<

# dr_mp3 as published, dr_mp3.h compiled as its implementation.
$(O)/sys/drlibs/dr_mp3.o: sys/drlibs/dr_mp3.h | $(DIRS)
	$(COMPILE) $(CFLAGS) -w -x c -DDR_MP3_IMPLEMENTATION -c -o $@ $<

$(addprefix $(O)/,$(ARRAYS)): $(O)/%.o: $(O)/%.c
	$(COMPILE) $(CFLAGS) $(NEWFLAGS) -c -o $@ $<

-include $(wildcard $(O)/*.d $(O)/*/*.d $(O)/*/*/*.d)

# test runs what needs the tree alone: the unit tests (test/unit) and the
# game's start without its files, cfgtest among its runs (test/nodata).
test: all
	test/unit $(O)
	test/nodata -x $(O)/lba$(EXE)

# The parts of check that a Linux target alone runs: the proof of the
# tunes cut from the CD's tracks, which runs pcm, and the start in real
# time with the music's stream watched and the game stalled, whose
# preload Wine's builds refuse.
CHECKDEPS_linux=$(O)/livemusic.so
CUTS_linux=python3 -I tools/cuts.py -x $(O)/pcm -c sys/cuts.c
LIVEMUSIC_linux=test/livemusic -x $(O)/lba

# check runs lint and test, then the tests on the game's files in
# LBATMP: the oracles, cfgtest on the CD's LBA.CFG, cdimagetest on the
# CDs' images, the proofs of the tables the C holds (the PlayStation's
# tracks of the tunes, sources/psmusic.c, the cells and the texts of
# the data's fixes, sources/gridfix.c and sources/textfix.c, and the
# places the scripts rewrite, sources/patch.c) and the records (test/e2e
# check).
check: lint test oraclecheck $(CHECKDEPS_$(OS))
	rm -rf $(O)/run/cfg && mkdir -p $(O)/run/cfg
	$(RUN) $(O)/cfgtest$(EXE) $(call HOSTPATH,$(O)/run/cfg) $(call HOSTPATH,$(LBATMP)/pc/LBA)
	rm -rf $(O)/run/cdimages
	$(RUN) $(O)/cdimagetest$(EXE) $(call HOSTPATH,$(O)/run/cdimages)\
		$(call HOSTPATH,$(LBAISO)/little_big_adventure_1.iso) $(call HOSTPATH,$(LBATMP)/pc) $(call HOSTPATH,$(LBATMP)/pc)\
		$(call HOSTPATH,$(LBATMP)/iso/adeline/ADELINE.CUE) $(call HOSTPATH,$(LBATMP)/pc) $(call HOSTPATH,$(LBATMP)/music/eu)\
		$(call HOSTPATH,$(LBATMP)/gog/lba1/Speedrun/Windows/LBA.DAT) $(call HOSTPATH,$(LBATMP)/gog/cd)\
		$(call HOSTPATH,$(LBATMP)/gog/cdtracks)\
		$(call HOSTPATH,$(LBATMP)/iso/ps/ps.cue) $(call HOSTPATH,$(LBATMP)/ps) $(call HOSTPATH,$(LBATMP)/psmusic)
	python3 -I tools/psmusic.py -c sources/psmusic.c
	python3 -I tools/grids.py -c sources/gridfix.c $(LBATMP)/pc/LBA/LBA_GRI.HQR\
		$(LBATMP)/gog/cd/LBA_GRI.HQR $(LBATMP)/gog/lba1/Speedrun/Windows/LBA_GRI.HQR
	python3 -I tools/patches.py -g $(LBATMP)/pc/LBA -p $(LBATMP)/ps -c sources/patch.c
	python3 -I tools/texts.py -c sources/textfix.c $(LBATMP)/gog/cd/TEXT.HQR\
		$(LBATMP)/pc/LBA/TEXT.HQR $(LBATMP)/gog/lba1/CommonClassic/TEXT.HQR\
		$(LBATMP)/ps/TEXT.HQR
	$(CUTS_$(OS))
	test/e2e check -x $(O)/lba$(EXE)
	$(LIVEMUSIC_$(OS))
