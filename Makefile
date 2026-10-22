# Makefile builds Little Big Adventure from Adeline's 1994 sources, for
# the target that OS, ARCH and CC name, into build/<target>. Plain make
# lists the goals.

# OS, ARCH and CC name the target: linux or windows, 386 or amd64, and
# gcc or clang, the game on SDL3, or dos, 386, and openwatcom
# (OpenWatcom 2) or watcom (Watcom C 9.01 and 9.5, which built
# LBA.EXE), the 1994 game for DOS. FIXES=1 builds the watcom target with
# the fixes of LBA.EXE's faults, which the code holds under FIXES and
# the port and openwatcom take always. SANITIZE=ubsan or SANITIZE=asan
# adds a sanitizer to a linux-amd64 target.
OS=linux
ARCH=amd64
CC=gcc
FIXES=
SANITIZE=

# LBATMP holds the game's files, the records, the downloads and the
# tools, by default ../lba1-tmp beside the tree, and the scripts the
# recipes run read it from the environment. LBAISO holds the CDs'
# images, by default ../iso beside the tree. The recipes' PWD is the
# tree's path as make has it, CURDIR, which gcc records as the build's
# directory and mk/port.mk's map takes out, where a symbolic link
# leading to the tree gave the shell's another path.
LBATMP?=../lba1-tmp
override LBATMP:=$(abspath $(LBATMP))
export LBATMP
export PWD:=$(CURDIR)
LBAISO?=../iso

# TARGETS lists the targets, each named as its directory.
TARGETS=\
	linux-386-gcc\
	linux-amd64-gcc\
	linux-386-clang\
	linux-amd64-clang\
	windows-386-gcc\
	windows-amd64-gcc\
	dos-386-openwatcom\
	dos-386-watcom\

ifneq ($(filter command% environment%,$(origin TARGET)),)
$(error TARGET names the build that OS, ARCH and CC give: make lists them)
endif
ifeq ($(filter $(OS)-$(ARCH)-$(CC),$(TARGETS)),)
$(error no target $(OS)-$(ARCH)-$(CC): the targets are $(strip $(TARGETS)))
endif
ifneq ($(filter-out 1,$(FIXES)),)
$(error FIXES=1 or nothing, where FIXES=$(FIXES))
endif
ifneq ($(FIXES),)
ifneq ($(CC),watcom)
$(error FIXES=1 builds CC=watcom alone: the port and openwatcom take the fixes always)
endif
endif
ifneq ($(filter-out ubsan asan,$(SANITIZE)),)
$(error SANITIZE=ubsan or SANITIZE=asan, where SANITIZE=$(SANITIZE))
endif
ifneq ($(SANITIZE),)
ifneq ($(OS)-$(ARCH),linux-amd64)
$(error SANITIZE builds a linux-amd64 target alone)
endif
endif

# TARGET names the build, O its directory, D its distribution, the
# folder dist builds, and ARCHIVE that folder's archive: a gzipped tar
# file for Linux, a zip file for Windows and DOS. DATE is the time of
# LBA.EXE's build in UTC, its version string's 11:43:15 in France: the
# archives' files take it.
TARGET=$(OS)-$(ARCH)-$(CC)$(if $(FIXES),-fixes)$(SANITIZE:%=-%)
O=build/$(TARGET)
D=dist/$(TARGET)
ARCHIVE_linux=.tar.gz
ARCHIVE_windows=.zip
ARCHIVE_dos=.zip
ARCHIVE=dist/lba1-$(TARGET)$(ARCHIVE_$(OS))
DATE=1994-10-14T10:43:15Z

# make keeps the target's variables out of the recipes' environment.
unexport OS ARCH CC FIXES SANITIZE

.DELETE_ON_ERROR:
.PHONY: usage fetch all dist test check oracles asan oraclecheck lint defs tables clean

# usage lists the goals.
usage::
	@echo 'usage: make goal ... [OS=linux|windows|dos] [ARCH=386|amd64]'
	@echo '                     [CC=gcc|clang|openwatcom|watcom] [FIXES=1] [SANITIZE=ubsan|asan]'
	@echo ''
	@echo 'the target that OS, ARCH, and CC name (linux, amd64, gcc unless given):'
	@echo '  fetch        install the tools the target needs into LBATMP (mk/fetch.mk)'
	@echo '  all          build the programs and the unit tests into build/<os>-<arch>-<cc>'
	@echo '  dist         build its distribution into dist/<os>-<arch>-<cc>, the programs'
	@echo '               and their licences, and the archive of that folder'
	@echo '  test         run the tests that need the tree alone'
	@echo "  check        run lint and test, then the tests on the game's files in LBATMP"
	@echo ''
	@echo 'the targets:'
	@echo '  $(wordlist 1,4,$(TARGETS))'
	@echo '  $(wordlist 5,8,$(TARGETS))'
	@echo '  and with FIXES=1 dos-386-watcom-fixes'
	@echo ''
	@echo 'the tree:'
	@echo '  oracles      build the oracles into build/oracle'
	@echo '  asan         build asmoracle-asan, asmoracle under AddressSanitizer, into build/oracle'
	@echo "  oraclecheck  run the oracles on the game's files"
	@echo "  lint         check the tree's rules a program can"
	@echo "  defs         make the game's .def files again (tools/defs)"
	@echo "  tables       make the EMU8000's tables again (tools/emu8ktab.py)"
	@echo '  clean        remove build/ and dist/'

# The port's lists of objects, in their link order. GAME and LIBS are
# the 1994 game's modules and libraries, which the port builds with
# sys/compat.h read first and their warnings off. NEW are the
# translations of the game's assembly and the game's new code, CARRIED
# LBA2's code carried in its own form, built as NEW but out of make
# lint's layout of the new C, PORT the layer on SDL3. Nuked OPL3
# (sys/nuked), dr_mp3 (sys/drlibs) and stb_vorbis (sys/stb) build as
# published, their warnings off, dr_mp3.h as its implementation.
GAME=\
	sources/ambiance.o\
	sources/diskfunc.o\
	sources/extra.o\
	sources/fiche.o\
	sources/flipbox.o\
	sources/gamemenu.o\
	sources/gerelife.o\
	sources/geretrak.o\
	sources/global.o\
	sources/grille.o\
	sources/holomap.o\
	sources/incrust.o\
	sources/message.o\
	sources/object.o\
	sources/perso.o\
	sources/playfla.o\
	sources/version.o\

LIBS=\
	lib386/lib_sys/def_file.o\
	lib386/lib_sys/divers.o\
	lib386/lib_sys/hq_mem.o\
	lib386/lib_sys/hq_r_m.o\
	lib386/lib_sys/hq_ress.o\
	lib386/lib_sys/loadmall.o\
	lib386/lib_sys/loadsave.o\
	lib386/lib_svga/gif.o\
	lib386/lib_svga/pcx.o\
	lib386/lib_svga/rect.o\
	lib386/lib_svga/s_text.o\

NEW=\
	lib386/lib_3d/p_anim.o\
	lib386/lib_3d/p_func.o\
	lib386/lib_3d/p_ob_iso.o\
	lib386/lib_3d/p_sintab.o\
	lib386/lib_3d/p_trigo.o\
	lib386/lib_svga/font_a.o\
	lib386/lib_svga/graph_a.o\
	lib386/lib_svga/graphmsk.o\
	lib386/lib_svga/mask_a.o\
	lib386/lib_svga/s_block.o\
	lib386/lib_svga/s_box.o\
	lib386/lib_svga/s_fillv.o\
	lib386/lib_svga/s_line.o\
	lib386/lib_svga/s_poly.o\
	lib386/lib_svga/s_screen.o\
	lib386/lib_svga/s_string.o\
	lib386/lib_svga/texture.o\
	lib386/lib_svga/zoom.o\
	lib386/lib_sys/buffer_a.o\
	lib386/lib_sys/compress.o\
	lib386/lib_sys/expand.o\
	lib386/lib_sys/hqr.o\
	lib386/lib_sys/settings.o\
	sources/adfli_a.o\
	sources/balance.o\
	sources/controls.o\
	sources/cpymask.o\
	sources/exttext.o\
	sources/fire.o\
	sources/func.o\
	sources/gridfix.o\
	sources/grille_a.o\
	sources/input.o\
	sources/lba2text.o\
	sources/mcga.o\
	sources/patch.o\
	sources/porttext.o\
	sources/psmusic.o\
	sources/textfix.o\

CARRIED=\
	lib386/lib_sys/lzss.o\
	sources/savegame.o\

PORT=\
	sys/awe.o\
	sys/awedata.o\
	sys/cd.o\
	sys/compat.o\
	sys/emu8k.o\
	sys/emu8kfx.o\
	sys/emu8ktab.o\
	sys/files.o\
	sys/keyb.o\
	sys/mem.o\
	sys/midi.o\
	sys/modes.o\
	sys/oplio.o\
	sys/qsort.o\
	sys/rand.o\
	sys/replay.o\
	sys/wide.o\
	sys/sdl.o\
	sys/sound.o\
	sys/sys.o\
	sys/timer.o\
	sys/video.o\
	sys/wave.o\
	sys/xmidi.o\
	sys/yamaha.o\
	sys/cuts.o\
	sys/cdimage.o\
	sys/input.o\
	sys/syskeys.o\

OPL3=\
	sys/nuked/opl3.o\

DECODE=\
	sys/drlibs/dr_mp3.o\
	sys/stb/stb_vorbis.o\

# sys/lba.cfg's text made into C at the build.
ARRAYS=\
	sys/lbacfg.o\

# The unit tests' objects, each list the test's own first, and those of
# the tool built beside the game.

# emu8ktest: the EMU8000 against its documents (test/emu8ktest.c).
EMU8KTEST=\
	test/emu8ktest.o\
	sys/emu8k.o\
	sys/emu8kfx.o\
	sys/emu8ktab.o\

# cfgtest: lba.cfg's reading and writing, through the port's files, its
# switches, LbaWin's settings and the music's sources it asks for.
CFGTEST=\
	test/cfgtest.o\
	lib386/lib_sys/def_file.o\
	lib386/lib_sys/loadmall.o\
	lib386/lib_sys/settings.o\
	sys/compat.o\
	sys/files.o\
	sys/mem.o\
	sys/modes.o\
	sys/cdimage.o\
	sys/lbacfg.o\

# inputtest: the Classic's input, its keys' virtual keys and the inputs
# its bindings give, read from lba.cfg (test/inputtest.c).
INPUTTEST=\
	test/inputtest.o\
	lib386/lib_sys/def_file.o\
	lib386/lib_sys/loadmall.o\
	lib386/lib_sys/settings.o\
	sources/input.o\
	sys/compat.o\
	sys/files.o\
	sys/input.o\
	sys/mem.o\
	sys/cdimage.o\

# syskeystest: sys/syskeys.c's answers to Alt+F4 and the Windows keys,
# the hook's and the game's, held to what each event must give
# (test/syskeystest.c).
SYSKEYSTEST=\
	test/syskeystest.o\
	sys/syskeys.o\

# padtest: sys/sdl.c's gamepad held to what each event must give, on
# SDL's virtual gamepad (test/padtest.c).
PADTEST=\
	test/padtest.o\
	sys/sdl.o\

# widetest: sys/wide.c's screenwidth, the wide screen's width held to
# the display's shape (test/widetest.c).
WIDETEST=\
	test/widetest.o\
	sys/wide.o\

# clocktest: sys/timer.c's clock driven as the game drives it, the
# retraces each loop of its calls waits for (test/clocktest.c).
CLOCKTEST=\
	test/clocktest.o\
	sys/timer.o\

# filestest: the preferences' files, held in memory and put on the disk
# by a writer, the test standing in for its thread.
FILESTEST=\
	test/filestest.o\
	lib386/lib_sys/def_file.o\
	lib386/lib_sys/loadmall.o\
	sys/compat.o\
	sys/files.o\
	sys/mem.o\
	sys/cdimage.o\

# cdimagetest: a CD's image read as the CD.
CDIMAGETEST=\
	test/cdimagetest.o\
	sys/compat.o\
	sys/files.o\
	sys/cdimage.o\

# pcm: a music file decoded as the port decodes it.
PCM=\
	tools/pcm.o\
	sys/decode.o\

# The game's modules whose .def files make defs writes, NEWDEFS the
# port's own, which include sys/compat.h themselves.
NEWDEFS=\
	controls\
	exttext\
	input\
	lba2text\
	porttext\
	psmusic\
	savegame\

DEFS=\
	ambiance\
	diskfunc\
	extra\
	fiche\
	flipbox\
	gamemenu\
	gerelife\
	geretrak\
	grille\
	holomap\
	incrust\
	message\
	object\
	perso\
	playfla\
	$(NEWDEFS)\

# lint checks with shellcheck every bash script, with tools/lint.py the
# tree's text, the new C in the layout of The Practice of Programming
# and the Python tools, on the standard library alone, and where LBA2's
# tree lies beside this one, the two ports' twin files held to each
# other (tools/twins.py).
lint:
	shellcheck -S warning $$(git grep -l --untracked '^#!/usr/bin/env bash')
	python3 -I tools/lint.py\
		-c $(NEW:.o=.c) $(PORT:.o=.c) $(wildcard sys/*.h dos/*.h test/*.c)\
			$(PCM:.o=.c)\
			$(wildcard tools/oracle/*.c tools/oracle/*.h tools/emu8kdiff/*.c)\
		-p $$(git ls-files -co --exclude-standard '*.py')\
		-- $$(git ls-files -co --exclude-standard)
	if [ -d ../lba2 ]; then python3 -I tools/twins.py ../lba2; fi

# defs makes each module's .def again, the prototypes of the functions
# it defines (tools/defs, tools/mkdefs.py), into build/defs first.
defs:
	tools/defs -o build/defs -n '$(strip $(NEWDEFS))' $(DEFS)

# tables makes sys/emu8ktab.c again with tools/emu8ktab.py and, where it
# changed, puts the new one in its place and stops make with an error.
tables:
	mkdir -p build && python3 -I tools/emu8ktab.py >build/emu8ktab.c
	cmp -s build/emu8ktab.c sys/emu8ktab.c ||\
		{ cp build/emu8ktab.c sys/emu8ktab.c && echo sys/emu8ktab.c changed && exit 1; }

# clean removes what make built.
clean:
	rm -rf build dist

# The rules: the tools make fetches, the game for DOS by the 1994
# compilers or the game on SDL3, then the oracles.
include mk/fetch.mk
ifeq ($(OS),dos)
include mk/watcom.mk
else
include mk/port.mk
endif
include mk/oracle.mk
