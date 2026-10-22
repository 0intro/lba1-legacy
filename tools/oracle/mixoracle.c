#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"
#include "tlba1c.h"
#include "dll.h"
#include "abi.h"
#include "pc.h"

/*
 * mixoracle: the card's mixer as lib386/lib_mix's mixer.c and
 * mixer_i.asm drive it through the CD's M_SB16.DLL, against
 * sys/sound.c's, the port's, at the interface the game calls:
 * MixerAskVars, MixerGetInfo, MixerGetVolume and MixerChangeVolume.
 *
 *	mixoracle lba0.elf lba0.sym M_SB16.DLL
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map).
 * The driver is loaded as MixerInitDLL loads it, by the image's
 * DLL_load into DOS memory, its first doubleword Mixer_listfcts, and its
 * BASE_ADDR is set to 220h through MixerAskVars, as InitAdelineSystem
 * sets MixerBase. It runs on pc.c's PC and a Sound Blaster 16's mixer,
 * the CT1745, its register at 224h and its data at 225h, each volume's
 * 5 bits read back as written, the chip as a reset leaves it: master,
 * voice and MIDI at 24 of 31, the CD and the line at 0. The port's
 * soundinit is given M_SB16.DLL for MixerDriver, NoWave for WaveDriver
 * and NoMidi for MidiDriver.
 *
 *	calls	the start's MixerGetInfo and MixerGetVolume, as
 *		ReadVolumeSettings makes them, then 100000 MixerChangeVolume
 *		of five volumes, the option menu's (a bar moved by 4 from 0
 *		to 255 for one of them, each other at 0 or 128) and any
 *		at random, -1 for one left as it is, each followed by
 *		MixerGetInfo and MixerGetVolume: the same answers, and the
 *		MIDI register's step the one the port gives its music
 *
 * The driver's calls must keep the registers C keeps. What the port
 * does otherwise on purpose is let pass: the right channels, which the
 * driver writes alike, the game reading the left, and the PC speaker's
 * and the outputs' switches (3Bh, 3Ch), which the driver sets at each
 * change, where the port's sources are the samples, the CD and the
 * music.
 *
 * Each part runs in a process of its own, ended after 600 seconds, its
 * differences reported, the first 20 in full, and the end is
 * "mixoracle: ok", or "mixoracle: N failed" and status 1.
 */

enum
{
	Rounds = 100000,
	Base = 0x220,
	Dllflags = 5,	/* DLLMEM_ALLOC|DLLSRC_MEM */
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	dllload;
	uintptr_t	askvars;
	uintptr_t	changevolume;
	uintptr_t	getvolume;
	uintptr_t	getinfo;
	uint32_t	*listfcts;
	uint32_t	*enable;
} as;

/* sys/sound.c's interface, which lib386/lib_mix/lib_mix.h declares for the game */
void	MixerChangeVolume(int32_t, int32_t, int32_t, int32_t, int32_t);
void	MixerGetVolume(int32_t*, int32_t*, int32_t*, int32_t*, int32_t*);
void	MixerGetInfo(int32_t*, int32_t*, int32_t*, int32_t*, int32_t*);

/* the CT1745 */
static uint8_t reg[256];
static int mixreg;	/* the register 224h chose */

static uint8_t *dll;	/* M_SB16.DLL's bytes */
static long ndll;
static int32_t pmidi = -1;	/* the MIDI volume the port gave its music */

/* def_file.c's, music.c's, midi.c's, modes.c's, sys.c's, wave.c's and sdl.c's, for sys/sound.c */
char	PathConfigFile[1];
int32_t	PlayRate = 22000;
int32_t	Wave_Driver_Enable;
int16_t	Midi_Driver_Enable;
int32_t	MusicTracks = Tcd;
int32_t	MusicJingles = Jmidi;
Tlba1c	tlba1c;

/* The configuration: M_SB16.DLL for the mixer, NoWave and NoMidi. */
unsigned char*
Def_ReadString(unsigned char *path, unsigned char *key)
{
	USED(path);
	if (strcmp((char*)key, "MixerDriver") == 0)
		return (unsigned char*)"M_SB16.DLL";
	if (strcmp((char*)key, "WaveDriver") == 0)
		return (unsigned char*)"NoWave";
	return (unsigned char*)"NoMidi";
}

int32_t
Def_ReadValue2(unsigned char *path, unsigned char *key, int32_t *v)
{
	USED(path);
	USED(key);
	USED(v);
	return 0;
}

int32_t
InitMusic(void)
{
	return 0;
}

void
SetMusicVolume(float g)
{
	USED(g);
}

void
SetVoiceVolume(float g)
{
	USED(g);
}

int
midiinit(void)
{
	return 0;
}

/* The music's volumes, from the MIDI register and the master. */
void
midimix(int32_t midi, int32_t master, float g)
{
	USED(master);
	USED(g);
	pmidi = midi;
}

void
sdlwavegain(float g)
{
	USED(g);
}

void
sdlcdgain(float g)
{
	USED(g);
}

static uint32_t
rnd(void)
{
	static uint32_t x = 0x2c1b3c6d;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* The mixer's ports: 224h the register, 225h its value, a volume's read back to its step. */
static uint32_t
mixin(uint32_t port, int w)
{
	USED(w);
	if (port == Base+4)
		return mixreg;
	if (mixreg >= 0x30 && mixreg <= 0x3a)
		return reg[mixreg] & 0xf8;
	return reg[mixreg];
}

static void
mixout(uint32_t port, uint32_t v, int w)
{
	USED(w);
	if (port == Base+4)
		mixreg = v & 0xff;
	else
		reg[mixreg] = v;
}

/* The chip after a reset, as Creative's CT1745 gives it. */
static void
reset(void)
{
	memset(reg, 0, sizeof reg);
	reg[0x30] = reg[0x31] = 0xc0;
	reg[0x32] = reg[0x33] = 0xc0;
	reg[0x34] = reg[0x35] = 0xc0;
	reg[0x3c] = 0x1f;
}

/* The driver loaded and given its base, as MixerInitDLL and InitAdelineSystem do. */
static void
load(void)
{
	uint32_t drvr, ids, vars;
	int32_t a[3];
	uint8_t *buf;

	buf = malloc(ndll);
	if (buf == NULL)
		fatal("out of memory");
	memmove(buf, dll, ndll);
	snprintf(what, sizeof what, "DLL_load");
	a[0] = (int32_t)(uintptr_t)buf;
	a[1] = Dllflags;
	a[2] = 0;
	drvr = abicall(as.dllload, 3, a);
	/* DLL_load is cdecl, which Watcom 9.01's STDDEF.H lets change EBX */
	if (abibroke & ~1u)
		abiok("DLL_load");
	if (drvr == 0)
		fatal("DLL_load refuses the driver");
	free(buf);
	*as.listfcts = get32((uint8_t*)(uintptr_t)drvr);
	*as.enable = 1;
	snprintf(what, sizeof what, "MixerAskVars");
	acall(as.askvars, 2, (int32_t)(uintptr_t)&ids, (int32_t)(uintptr_t)&vars);
	put32((uint8_t*)(uintptr_t)vars, Base);
}

/* MixerGetInfo and MixerGetVolume on both sides, the same. */
static void
same(void)
{
	int32_t a[5], p[5];
	int i;

	for (i = 0; i < 5; i++)
		a[i] = p[i] = 0x55;
	acall(as.getinfo, 5, (int32_t)(uintptr_t)&a[0], (int32_t)(uintptr_t)&a[1],
		(int32_t)(uintptr_t)&a[2], (int32_t)(uintptr_t)&a[3], (int32_t)(uintptr_t)&a[4]);
	MixerGetInfo(&p[0], &p[1], &p[2], &p[3], &p[4]);
	if (memcmp(a, p, sizeof a) != 0) {
		fail("MixerGetInfo: the assembly's %d %d %d %d %d, ours %d %d %d %d %d",
			a[0], a[1], a[2], a[3], a[4], p[0], p[1], p[2], p[3], p[4]);
		return;
	}
	for (i = 0; i < 5; i++)
		a[i] = p[i] = 0x55;
	acall(as.getvolume, 5, (int32_t)(uintptr_t)&a[0], (int32_t)(uintptr_t)&a[1],
		(int32_t)(uintptr_t)&a[2], (int32_t)(uintptr_t)&a[3], (int32_t)(uintptr_t)&a[4]);
	MixerGetVolume(&p[0], &p[1], &p[2], &p[3], &p[4]);
	if (memcmp(a, p, sizeof a) != 0) {
		fail("MixerGetVolume: the assembly's %d %d %d %d %d, ours %d %d %d %d %d",
			a[0], a[1], a[2], a[3], a[4], p[0], p[1], p[2], p[3], p[4]);
		return;
	}
	if ((reg[0x34] & 0xf8) != (pmidi & 0xf8)) {
		fail("the MIDI register %02x, the port's music given %02x", reg[0x34], pmidi & 0xff);
	}
}

/* A volume as the game or chance gives it. */
static int32_t
anyvolume(void)
{
	switch (rnd() % 4) {
	case 0:
		return -1;
	case 1:
		return rnd();
	}
	return rnd() % 256;
}

static void
calls(void *v)
{
	int32_t x[5], bar;
	int i, j, k;

	USED(v);
	reset();
	load();
	soundinit();
	snprintf(what, sizeof what, "the start");
	same();
	bar = 128;
	for (i = 0; i < Rounds && nfail == 0; i++) {
		if (rnd() % 2 == 0) {
			bar += rnd() % 2 ? 4 : -4;
			if (bar < 0)
				bar = 0;
			if (bar > 255)
				bar = 255;
			k = rnd() % 5;
			for (j = 0; j < 5; j++)
				x[j] = j == k ? bar : rnd() % 2 ? 0 : 128;
		} else {
			for (j = 0; j < 5; j++)
				x[j] = anyvolume();
		}
		snprintf(what, sizeof what, "call %d, MixerChangeVolume(%d, %d, %d, %d, %d)", i,
			x[0], x[1], x[2], x[3], x[4]);
		acall(as.changevolume, 5, x[0], x[1], x[2], x[3], x[4]);
		MixerChangeVolume(x[0], x[1], x[2], x[3], x[4]);
		same();
	}
	printf("calls: %d calls, %s\n", i, nfail == 0 ? "the same" : "differ");
}

int
main(int argc, char **argv)
{
	int nbad;

	oracle = "mixoracle";
	if (argc != 4) {
		fprintf(stderr, "usage: mixoracle lba0.elf lba0.sym M_SB16.DLL\n");
		return 2;
	}
	pcload(argv[1], argv[2]);
	as.dllload = lookup("_DLL_load");
	as.askvars = lookup("MixerAskVars");
	as.changevolume = lookup("MixerChangeVolume");
	as.getvolume = lookup("MixerGetVolume");
	as.getinfo = lookup("MixerGetInfo");
	as.listfcts = (uint32_t*)lookup("Mixer_listfcts");
	as.enable = (uint32_t*)lookup("Mixer_Driver_Enable");
	dll = readfile(argv[3], &ndll);
	pcport(Base+4, Base+5, mixin, mixout);
	nbad = inchild("calls", 600, calls, NULL);
	if (nbad > 0) {
		fprintf(stderr, "mixoracle: %d failed\n", nbad);
		return 1;
	}
	printf("mixoracle: ok\n");
	return 0;
}
