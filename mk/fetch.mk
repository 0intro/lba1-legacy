# fetch.mk installs the tools a target's build needs into $(LBATMP)/tools,
# each from its archive in $(LBATMP)/downloads, which make fetch
# downloads there when it is missing and holds to its SHA-256. It alone
# names the versions of what make fetches.

# Each tool's version, the URL of its archive and the archive's SHA-256:
# JWasm, which assembles OpenWatcom's build's assembly, OpenWatcom 2 by
# the date of its snapshot, Watcom C 9.01 and 9.5b, MASM 6.11 and Turbo
# Assembler 3.1, which built LBA.EXE, the last from the floppies of
# Borland C++ 3.1, which shipped it, Watcom C/C++ 10.0a, whose qsort
# both DOS builds link first and whose wlink links Watcom C 9's wave
# drivers. 9.01 takes Watcom's patches B to D of 1992, each a level, in
# their order, as WinWorld's floppies hold level A: LBA.EXE links level
# D's library. They lie on Walnut Creek's Hobbes OS/2 CD of October
# 1993, which discmaster serves. WinWorld's mirror serves an archive to
# a client once the site's download link has sent it there: its URLs are
# those links.
JWASM_VERSION=2.20
JWASM_URL=https://github.com/Baron-von-Riedesel/JWasm/archive/refs/tags/v$(JWASM_VERSION).tar.gz
JWASM_SHA256=a9b78dfe18af47ea9eb55fdee0bf1fb5b8b3d9a8f3d0a490cf6b2b984151ccde

OW2_VERSION=2026-10-01
OW2_URL=https://github.com/open-watcom/open-watcom-v2/releases/download/$(OW2_VERSION)-Build/ow-snapshot.tar.xz
OW2_SHA256=e6aa1b1e40ac8bbf97658d2c70fff8a4242d6ca4a1c60806f2baa5317083d4fe

W901_VERSION=9.01
W901_URL=https://winworldpc.com/download/caed08b0-8d27-11ec-84e0-0200008a0da4/from/c39ac2af-c381-c2bf-1b25-11c3a4e284a2
W901_SHA256=b90d7b6d668f8eec196fae70f6e568a0f2563f181253bc331f2165f46367183c
W901_PATCHES=b c d
W901_PATCH_URL=https://discmaster.textfiles.com/file/40109/Hobbes_OS2_CD-ROM_Walnut_Creek_October_1993.iso/2_x/patches
W901_PATCH_SHA256_b=2d41b4531aa43a90440f0406a95282d159602a4a598a9368b5c8c2d326f8ca46
W901_PATCH_SHA256_c=04a9133c46f44d2a079ef16ecbfb5877c6fcf784c70817b99d194357834ac80c
W901_PATCH_SHA256_d=5a27816d4d6dd036ad26ff43c795cd3101f93c47f3d521474a12b20c1ae9d2da

W95_VERSION=9.5b
W95_URL=https://winworldpc.com/download/e531a16b-6932-11ed-aca0-0200008a0da4/from/c39ac2af-c381-c2bf-1b25-11c3a4e284a2
W95_SHA256=6aef8fd831c540854e026e3be9de6887b4c3dc27d06a08801fd32472c38db7e9

W100A_VERSION=10.0a
W100A_URL=https://winworldpc.com/download/46c3bfc2-ade2-809c-18c3-9a11c3a4efbf/from/c39ac2af-c381-c2bf-1b25-11c3a4e284a2
W100A_SHA256=37f2ac13b414e441fe2a76df372263af433b142cf1de72ec431d4a7a2622568a

ML_VERSION=6.11
ML_URL=https://winworldpc.com/download/c3bcc389-53c3-b2c2-a1c3-ac11c3a4c2ac/from/c39ac2af-c381-c2bf-1b25-11c3a4e284a2
ML_SHA256=8aa662e8f4b4b7cf4c2d5f87ae31484dcce7f23cd89a6d82a93ab9d9f37b4be5

TASM_VERSION=3.1
TASM_URL=https://winworldpc.com/download/f7396558-9ba2-11e9-ab10-fa163e9022f0/from/c39ac2af-c381-c2bf-1b25-11c3a4e284a2
TASM_SHA256=76e155a7fb7eb0590930ecd513644c977497a020466a272943776fbf683d0f27

# DL holds the downloads, each named by its version, and TOOLS the
# tools: JWasm in bin, OpenWatcom 2 in ow2, Watcom C 9.01, 9.5b, 10.0a's
# library and linker, MASM 6.11 and TASM 3.1 where dos/watcom installs
# them.
DL=$(LBATMP)/downloads
TOOLS=$(LBATMP)/tools
JWASM_TGZ=$(DL)/jwasm-$(JWASM_VERSION).tar.gz
OW2_TXZ=$(DL)/ow-snapshot-$(OW2_VERSION).tar.xz
W901_7Z=$(DL)/watcom-$(W901_VERSION).7z
W901_ZIP=$(W901_PATCHES:%=$(DL)/watcom-$(W901_VERSION)%.zip)
W95_7Z=$(DL)/watcom-$(W95_VERSION).7z
W100A_7Z=$(DL)/watcom-$(W100A_VERSION).7z
ML_7Z=$(DL)/masm-$(ML_VERSION).7z
TASM_7Z=$(DL)/bcpp-$(TASM_VERSION).7z
JWASMEXE=$(abspath $(TOOLS)/bin/jwasm)
OW2=$(abspath $(TOOLS)/ow2)
W901=$(abspath $(TOOLS)/w901)
W95=$(abspath $(TOOLS)/w95)
W100A=$(abspath $(TOOLS)/w100a)
MASM=$(abspath $(TOOLS)/masm)
TASM=$(abspath $(TOOLS)/tasm)

# Each tool's stamp, named by its version, which make fetch writes once
# the tool is installed. The rules that run a tool name its stamp, so
# that a build stops at a tool that is missing or at another version.
# 9.01's is named by its last patch's level.
JWASM_OK=$(TOOLS)/jwasm-$(JWASM_VERSION).ok
OW2_OK=$(TOOLS)/ow2-$(OW2_VERSION).ok
W901_OK=$(TOOLS)/w901-$(W901_VERSION)$(lastword $(W901_PATCHES)).ok
W95_OK=$(TOOLS)/w95-$(W95_VERSION).ok
W100A_OK=$(TOOLS)/w100a-$(W100A_VERSION).ok
ML_OK=$(TOOLS)/masm-$(ML_VERSION).ok
TASM_OK=$(TOOLS)/tasm-$(TASM_VERSION).ok

# fetch installs what the target needs: 10.0a's library and linker and
# the tools that CC names: OpenWatcom 2 and JWasm, or Watcom C 9.01 and
# its patches, 9.5b, MASM 6.11 and TASM 3.1.
FETCH_dos=\
	$(W100A_OK)\

FETCH_openwatcom=\
	$(OW2_OK)\
	$(JWASM_OK)\

FETCH_watcom=\
	$(W901_OK)\
	$(W95_OK)\
	$(ML_OK)\
	$(TASM_OK)\

fetch: $(FETCH_$(OS)) $(FETCH_$(CC))

ifeq ($(filter fetch,$(MAKECMDGOALS)),)
# Outside make fetch a stamp that is missing stops the build.
$(JWASM_OK) $(OW2_OK) $(W901_OK) $(W95_OK) $(W100A_OK) $(ML_OK) $(TASM_OK):
	@echo "no $@: run make fetch OS=$(OS) ARCH=$(ARCH) CC=$(CC)" >&2; exit 1
else
# CURL downloads a URL, and tries again when the network or the server
# fails.
CURL=curl -fsSL --retry 5 --retry-all-errors

# FETCH downloads the URL $(1) into DL/tmp, holds it to the SHA-256 $(2),
# then gives it its place, $@.
define FETCH
mkdir -p $(DL)/tmp && $(CURL) -o $(DL)/tmp/$(@F) '$(1)'
echo '$(2)  $(DL)/tmp/$(@F)' | sha256sum --quiet -c
mv $(DL)/tmp/$(@F) $@
endef

# CHECK holds the archive $< to the SHA-256 $(1), as one found in DL
# may come from elsewhere.
CHECK=echo '$(1)  $<' | sha256sum --quiet -c

$(JWASM_TGZ):
	$(call FETCH,$(JWASM_URL),$(JWASM_SHA256))
$(OW2_TXZ):
	$(call FETCH,$(OW2_URL),$(OW2_SHA256))
$(W901_7Z):
	$(call FETCH,$(W901_URL),$(W901_SHA256))
$(W901_ZIP): $(DL)/watcom-$(W901_VERSION)%.zip:
	$(call FETCH,$(W901_PATCH_URL)/c386_$*.zip,$(W901_PATCH_SHA256_$*))
$(W95_7Z):
	$(call FETCH,$(W95_URL),$(W95_SHA256))
$(W100A_7Z):
	$(call FETCH,$(W100A_URL),$(W100A_SHA256))
$(ML_7Z):
	$(call FETCH,$(ML_URL),$(ML_SHA256))
$(TASM_7Z):
	$(call FETCH,$(TASM_URL),$(TASM_SHA256))

# JWasm built from its source by its own makefile, which takes none of
# this make's variables.
$(JWASM_OK): $(JWASM_TGZ)
	$(call CHECK,$(JWASM_SHA256))
	rm -rf $(TOOLS)/jwasm-src && mkdir -p $(TOOLS)/jwasm-src $(TOOLS)/bin
	tar -xzf $< -C $(TOOLS)/jwasm-src --strip-components=1
	MAKEFLAGS= make -C $(TOOLS)/jwasm-src -f GccUnix.mak
	cp $(TOOLS)/jwasm-src/build/GccUnixR/jwasm $(JWASMEXE)
	rm -f $(TOOLS)/jwasm-*.ok && touch $@

# OpenWatcom 2's compiler, librarian and linker for Linux, its headers,
# its DOS libraries and DOS/4GW.
$(OW2_OK): $(OW2_TXZ)
	$(call CHECK,$(OW2_SHA256))
	rm -rf $(OW2) && mkdir -p $(OW2)
	tar -xJf $< -C $(OW2) ./binl64/wcc386 ./binl64/wlib ./binl64/wlink ./binl64/wlink.lnk\
		./binl64/wstub.exe ./binw/dos4gw.exe ./h ./lib386/dos
	rm -f $(TOOLS)/ow2-*.ok && touch $@

# Watcom C 9.01 and 9.5b installed from their floppies by their own
# INSTALL.EXE in DOSBox, 9.01's patches then applied in order by their
# own batches, 10.0a's library and linker taken from its CD, MASM 6.11's
# ML expanded from its first floppy by its own DECOMP.EXE, and TASM
# 3.1's TASM.EXE taken from the ZIP on Borland C++ 3.1's floppies.
$(W901_OK): $(W901_7Z) $(W901_ZIP)
	$(call CHECK,$(W901_SHA256))
	printf '%s  %s\n' $(foreach p,$(W901_PATCHES),$(W901_PATCH_SHA256_$(p)) $(DL)/watcom-$(W901_VERSION)$(p).zip) |\
		sha256sum --quiet -c
	dos/watcom install $< $(W901_ZIP)
	rm -f $(TOOLS)/w901-*.ok && touch $@
$(W95_OK): $(W95_7Z)
	$(call CHECK,$(W95_SHA256))
	dos/watcom install $<
	rm -f $(TOOLS)/w95-*.ok && touch $@
$(W100A_OK): $(W100A_7Z)
	$(call CHECK,$(W100A_SHA256))
	dos/watcom install $<
	rm -f $(TOOLS)/w100a-*.ok && touch $@
$(ML_OK): $(ML_7Z)
	$(call CHECK,$(ML_SHA256))
	dos/watcom install $<
	rm -f $(TOOLS)/masm-*.ok && touch $@
$(TASM_OK): $(TASM_7Z)
	$(call CHECK,$(TASM_SHA256))
	dos/watcom install $<
	rm -f $(TOOLS)/tasm-*.ok && touch $@
endif
