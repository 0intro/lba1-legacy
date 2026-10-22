# Makefile builds Little Big Adventure from Adeline's 1994 sources, for
# the target that OS, ARCH and CC name, into build/<target>. Plain make
# lists the goals.

# OS, ARCH and CC name the target: dos, 386, and openwatcom
# (OpenWatcom 2) or watcom (Watcom C 9.01 and 9.5, which built LBA.EXE),
# the 1994 game for DOS. FIXES=1 builds the watcom target with the fixes
# of LBA.EXE's faults, which the code holds under FIXES and openwatcom
# takes always.
OS=dos
ARCH=386
CC=openwatcom
FIXES=

# LBATMP holds the game's files, the downloads and the tools, by default
# ../lba1-tmp beside the tree, and the scripts the recipes run read it
# from the environment.
LBATMP?=../lba1-tmp
override LBATMP:=$(abspath $(LBATMP))
export LBATMP

# TARGETS lists the targets, each named as its directory.
TARGETS=\
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
$(error FIXES=1 builds CC=watcom alone: openwatcom takes the fixes always)
endif
endif

# TARGET names the build, O its directory, D its distribution, the
# folder dist builds, and ARCHIVE that folder's archive, a zip file.
# DATE is the time of LBA.EXE's build in UTC, its version string's
# 11:43:15 in France: the archives' files take it.
TARGET=$(OS)-$(ARCH)-$(CC)$(if $(FIXES),-fixes)
O=build/$(TARGET)
D=dist/$(TARGET)
ARCHIVE_dos=.zip
ARCHIVE=dist/lba1-$(TARGET)$(ARCHIVE_$(OS))
DATE=1994-10-14T10:43:15Z

# make keeps the target's variables out of the recipes' environment.
unexport OS ARCH CC FIXES

.DELETE_ON_ERROR:
.PHONY: usage fetch all dist test check oracles asan oraclecheck lint defs clean

# usage lists the goals.
usage::
	@echo 'usage: make goal ... [OS=dos] [ARCH=386] [CC=openwatcom|watcom] [FIXES=1]'
	@echo ''
	@echo 'the target that OS, ARCH, and CC name (dos, 386, openwatcom unless given):'
	@echo '  fetch        install the tools the target needs into LBATMP (mk/fetch.mk)'
	@echo '  all          build the programs into build/<os>-<arch>-<cc>'
	@echo '  dist         build its distribution into dist/<os>-<arch>-<cc>, the programs'
	@echo '               and their licences, and the archive of that folder'
	@echo '  test         run the tests that need the tree alone'
	@echo "  check        run lint and test, then the tests on the game's files in LBATMP"
	@echo ''
	@echo 'the targets:'
	@echo '  $(strip $(TARGETS))'
	@echo '  and with FIXES=1 dos-386-watcom-fixes'
	@echo ''
	@echo 'the tree:'
	@echo '  oracles      build the oracles into build/oracle'
	@echo '  asan         build asmoracle-asan, asmoracle under AddressSanitizer, into build/oracle'
	@echo "  oraclecheck  run the oracle on the game's LBA.EXE"
	@echo "  lint         check the tree's rules a program can"
	@echo "  defs         make the game's .def files again (tools/defs)"
	@echo '  clean        remove build/ and dist/'

# NEW: the translations of the game's assembly into C, which asmoracle
# holds to the assembly and lint reads.
NEW=\
	lib386/lib_3d/p_anim.c\
	lib386/lib_3d/p_func.c\
	lib386/lib_3d/p_ob_iso.c\
	lib386/lib_3d/p_sintab.c\
	lib386/lib_3d/p_trigo.c\
	lib386/lib_svga/font_a.c\
	lib386/lib_svga/graph_a.c\
	lib386/lib_svga/graphmsk.c\
	lib386/lib_svga/mask_a.c\
	lib386/lib_svga/s_block.c\
	lib386/lib_svga/s_box.c\
	lib386/lib_svga/s_fillv.c\
	lib386/lib_svga/s_line.c\
	lib386/lib_svga/s_poly.c\
	lib386/lib_svga/s_screen.c\
	lib386/lib_svga/texture.c\
	lib386/lib_sys/buffer_a.c\
	lib386/lib_sys/expand.c\
	lib386/lib_sys/hqr.c\
	sources/adfli_a.c\
	sources/balance.c\
	sources/cpymask.c\
	sources/fire.c\
	sources/func.c\
	sources/grille_a.c\
	sources/mcga.c\

# The game's modules whose .def files make defs writes.
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

# lint checks with shellcheck every bash script, with tools/lint.py the
# tree's text, the new C in the layout of The Practice of Programming
# and the Python tools, on the standard library alone.
lint:
	shellcheck -S warning $$(git grep -l --untracked '^#!/usr/bin/env bash')
	python3 -I tools/lint.py\
		-c $(NEW) sys/qsort.c sys/rand.c $(wildcard sys/*.h dos/*.h)\
			$(wildcard tools/oracle/*.c tools/oracle/*.h)\
		-p $$(git ls-files -co --exclude-standard '*.py')\
		-- $$(git ls-files -co --exclude-standard)

# defs makes each module's .def again, the prototypes of the functions
# it defines (tools/defs, tools/mkdefs.py), into build/defs first.
defs:
	tools/defs -o build/defs $(DEFS)

# clean removes what make built.
clean:
	rm -rf build dist

# The rules: the tools make fetches, the game for DOS by the 1994
# compilers, then the oracles.
include mk/fetch.mk
include mk/watcom.mk
include mk/oracle.mk
