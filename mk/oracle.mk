# oracle.mk builds the oracles into build/oracle, 32-bit, at a fixed
# address, what they run mapped below them: oracle, LBA.EXE's own
# functions against ours (oracle.c), asmoracle, a DOS build's assembly
# against its translations into C (asmoracle.c, abi.c), and
# asmoracle-asan, the same under AddressSanitizer. On a DOS target with
# the fixes, make test runs oracle and asmoracle, and on LBA.EXE's code
# (CC=watcom without FIXES) oracle.

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

# the headers the oracles read, the game's, those of sys/ and their own,
# so that a change of one builds every oracle again
ORACLEHDR=$(wildcard sources/*.h sources/*.def sys/*.h lib386/*/*.h tools/oracle/*.h)

# oracles builds the oracles.
oracles: $(ORACLE)/oracle $(ORACLE)/asmoracle

# asan builds asmoracle-asan.
asan: $(ORACLE)/asmoracle-asan

$(ORACLE)/oracle: $(ORACLESRC) $(ORACLEHDR)
	mkdir -p $(ORACLE)
	$(ORACLECC) $(ORACLECFLAGS) $(ORACLELDFLAGS) -o $@ $(ORACLESRC)

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

# oraclecheck runs the oracle on the game's files in LBATMP: LBA.EXE's
# image as tools/extract writes it.
oraclecheck: $(ORACLE)/oracle
	$(ORACLE)/oracle $(LBATMP)/elf/lba.elf re/lba.exe.sym

-include $(ASMOBJ:.o=.d) $(ASANOBJ:.o=.d)
