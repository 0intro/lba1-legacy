#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"
#include "nuked/opl3.h"

/*
 * The music, as lib386/lib_midi/midi.c played it through AIL/32: the
 * XMIDI driver lba.cfg's MidiDriver names, A32GFX.DLL's for the Sound
 * Blaster AWE32 (awe.c), any other A32SP2FM.DLL's for the OPL3
 * (yamaha.c), its sequencer (xmidi.c) under the game's calls (PlayMidi
 * and its kin), the timbres it asks for from the Global Timbre Library,
 * "SAMPLE." and the driver's suffix, loaded whole. AIL/32 served the
 * driver from the timer it set to the driver's 120 Hz, a PIT divisor of
 * 9943. Here the clock serves it as it passes each of those periods
 * (miditick).
 *
 * The OPL3 is Nuked OPL3 at its 49716 Hz, given the registers the
 * driver writes, and its ports are modelled for the driver's detection
 * alone (oplio.c): the timers' flags in its status, the card's mixer.
 * The AWE32's EMU8000 is emu8k.c at its 44100 Hz, from the sound ROM in
 * awe32.raw, driven through its ports on a time of its own: what it
 * makes while the driver sets it up stays out of the music, the clock's
 * first sample following it, and the driver's calls land at its time. The
 * chip's samples go to their own stream, a timer period's at a time, at
 * the mixer's MIDI and master volumes: the OPL3's applied to the
 * stream, the AWE32's to its 32-bit sum before it is cut to 16 bits.
 * The driver's memory is the C library's, as midi.c's was, not the
 * game's.
 */

enum
{
	Pitdivisor = 9943,
	Pithz = 1193182,
	Oplhz = 49716,
	Awehz = 44100,
	Nbuf = 4096,	/* frames handed on at most at once */
	Romwords = 0x80000,
};

int16_t	Midi_Driver_Enable;
int32_t	MaxVolume = 100;

static Synth *synth;
static Drvrdesc *desc;
static int awe;		/* 1: the AWE32's driver, 0: the OPL3's */
static uint32_t iobase;
static opl3_chip opl;
static uint16_t *rom;
static uint8_t *gtl;
static uint8_t *state;
static uint8_t *cache;
static int32_t hseq = -1;
static int heard;		/* the chip's samples handed on */
static uint64_t ticks;		/* served */
static uint64_t samples;	/* handed on */
static uint64_t before;	/* the EMU8000's, made before the clock's first */
static int16_t buf[2*Nbuf];
static int nbuf;
static uint32_t awegain = 65536;	/* the mixer's on the AWE32, Q16 */

/* 10^((k-31)/10), Q16: an SB16 mixer register's step k of 2 dB (sound.c's gain, in integers) */
static uint32_t mixgain[32] = {
	52, 66, 83, 104, 131, 165, 207, 261, 328, 414, 521, 655, 825, 1039, 1308, 1646,
	2072, 2609, 3285, 4135, 5206, 6554, 8250, 10387, 13076, 16462, 20724, 26090, 32846, 41350, 52057, 65536,
};

/* The frames made since last handed on, to the stream and the record. */
static void
handon(void)
{
	if (nbuf == 0)
		return;
	samples += nbuf;
	sdlmidi(buf, nbuf, awe ? Awehz : Oplhz);
	summedmidi(samples, buf, nbuf);
	nbuf = 0;
}

/* A sample of 32 bits scaled by g (Q16) and cut to 16. */
static int16_t
scale(int32_t v, uint32_t g)
{
	int64_t x;

	x = (int64_t)v * g;
	x = x >= 0 ? x >> 16 : -((-x - 1) >> 16) - 1;
	return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

/*
 * The EMU8000's frames, kept to be handed on once they are heard: at
 * the mixer's gain, then cut to 16 bits.
 */
static void
aweframes(int32_t *o, int n)
{
	int i;

	if (!heard)
		return;
	for (i = 0; i < n; i++) {
		buf[2*nbuf] = scale(o[2*i], awegain);
		buf[2*nbuf+1] = scale(o[2*i+1], awegain);
		if (++nbuf == Nbuf)
			handon();
	}
}

/*
 * midimix: the mixer's MIDI and master registers and their gain: the
 * OPL3's stream at the gain, the AWE32's samples, the chip's 32 bits,
 * scaled by the registers' steps before they are cut to 16, as the
 * card's mixer took them before its codec.
 */
void
midimix(int32_t midi, int32_t master, float g)
{
	if (!awe) {
		sdlmidigain(g);
		return;
	}
	awegain = (uint32_t)((uint64_t)mixgain[(midi & 0xff) >> 3] * mixgain[(master & 0xff) >> 3] >> 16);
	sdlmidigain(1);
}

/* oplwrite: a register of the chip written, array 1 above 0xff. */
void
oplwrite(uint32_t reg, uint32_t val)
{
	OPL3_WriteReg(&opl, reg, val);
}

/* oplout: a byte to a port of the card (oplio.c). */
void
oplout(uint32_t port, uint32_t val)
{
	oplportout(port, val);
}

/* oplin: a byte from a port of the card. */
uint32_t
oplin(uint32_t port)
{
	return oplportin(port);
}

/* aweout: a word to a port of the card, the EMU8000's from the base. */
void
aweout(uint32_t port, uint32_t val)
{
	emu8kout((port - iobase) & 0xffff, val);
}

/* awein: a word from a port of the card. */
uint32_t
awein(uint32_t port)
{
	return emu8kin((port - iobase) & 0xffff);
}

/*
 * The AWE32's sound ROM, awe32.raw in the preferences or the game's
 * directory: 1 MB of little-endian words, "1MGM" at the third. A dump
 * one word late (AWE-DUMP's, 0x1234 first, the last word lost) is put
 * back as 86Box puts it. NULL if there is none.
 */
static uint16_t*
loadrom(void)
{
	char path[1024];
	uint8_t *b;
	uint16_t *r;
	size_t n;
	uint32_t i;
	FILE *f;

	if (readpath("awe32.raw", path, sizeof path) < 0)
		return NULL;
	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	b = malloc(2*Romwords + 1);
	r = malloc(2*Romwords);
	n = 0;
	if (b != NULL && r != NULL)
		n = fread(b, 1, 2*Romwords + 1, f);
	fclose(f);
	if (n != 2*Romwords) {
		fprintf(stderr, "lba: %s: %lu bytes, an AWE32 sound ROM's %lu\n", path,
			(unsigned long)n, (unsigned long)(2*Romwords));
		free(b);
		free(r);
		return NULL;
	}
	for (i = 0; i < Romwords; i++)
		r[i] = b[2*i] | b[2*i+1]<<8;
	free(b);
	if (r[3] == 0x314d && r[4] == 0x474d) {
		memmove(r, r+1, 2*(Romwords-1));
		r[Romwords-1] = 0;
	}
	if (r[2] != 0x314d || r[3] != 0x474d) {
		fprintf(stderr, "lba: %s: its words are no AWE32 sound ROM's, 1MGM missing\n", path);
		free(r);
		return NULL;
	}
	return r;
}

/*
 * midiinit is midi.c's InitMidiDLL and InitMidi, AIL/32's calls as they
 * made them: the driver lba.cfg names, its card at MidiBase (MidiIRQ
 * and MidiDMA read by name, kept in the descriptor), looked for and set
 * up, its timbre cache given it if it wants one, its state made, the
 * GTL loaded: 1, or 0 with the reason said.
 */
int
midiinit(void)
{
	char gtlname[16];
	unsigned char *p;
	int32_t base, irq, dma;
	uint32_t n;

	p = Def_ReadString((unsigned char*)PathConfigFile, (unsigned char*)"MidiDriver");
	awe = p != NULL && watcomstricmp((char*)p, "A32GFX.DLL") == 0;
	synth = awe ? &awe32 : &opl3;
	desc = synth->desc;
	/*
	 * FIXED: LBA.EXE writes MidiIRQ and MidiDMA after MidiBase, through
	 * AskMidiVars's pointer, into variables other than those InitMidi
	 * reads, MidiDMA into the driver's handle. Each key is read by name
	 * into the descriptor.
	 */
	if (!Def_ReadValue2((unsigned char*)PathConfigFile, (unsigned char*)"MidiBase", &base)
	|| !Def_ReadValue2((unsigned char*)PathConfigFile, (unsigned char*)"MidiIRQ", &irq)
	|| !Def_ReadValue2((unsigned char*)PathConfigFile, (unsigned char*)"MidiDMA", &dma)) {
		fprintf(stderr, "lba: no music: %s lacks MidiBase, MidiIRQ or MidiDMA\n", PathConfigFile);
		return 0;
	}
	if (base != -1)
		desc->io = base;
	if (irq != -1)
		desc->irq = irq;
	if (dma != -1) {
		desc->dma = dma;
		desc->drq = dma;
	}
	iobase = desc->io;
	if (awe) {
		rom = loadrom();
		if (rom == NULL) {
			fprintf(stderr, "lba: no music: no AWE32 sound ROM (awe32.raw) in the preferences or the game\n");
			return 0;
		}
		emu8kreset(rom, aweframes);
	} else {
		OPL3_Reset(&opl, Oplhz);
		oplioreset(iobase);
	}
	if (!synth->detect(desc->io, desc->irq, desc->dma, desc->drq)) {
		fprintf(stderr, "lba: no music: Sound Hardware not found\n");
		return 0;
	}
	initdriver(synth, desc->io, desc->irq, desc->dma, desc->drq);
	n = synth->cachesize();
	if (n != 0) {
		cache = calloc(1, n);
		if (cache == NULL) {
			fprintf(stderr, "lba: no music: out of memory\n");
			return 0;
		}
		synth->definecache(cache, n);
	}
	state = calloc(1, statesize());
	if (state == NULL) {
		fprintf(stderr, "lba: no music: out of memory\n");
		return 0;
	}
	snprintf(gtlname, sizeof gtlname, "SAMPLE.%s", desc->suffix);
	n = FileSize(gtlname);
	if (n == 0 || (gtl = malloc(n)) == NULL || Load(gtlname, gtl) != n) {
		fprintf(stderr, "lba: no music: cannot load %s\n", gtlname);
		return 0;
	}
	if (awe)
		before = emu8kmade();
	heard = 1;
	return 1;
}

/* ClearMidi is midi.c's: AIL/32 shut down, the driver signed off, out of the music. */
void
ClearMidi(void)
{
	if (!Midi_Driver_Enable)
		return;
	heard = 0;
	shutdowndriver("");
	hseq = -1;
}

/* load_global_timbre is midi.c's: the GTL's timbre of the bank and patch, or NULL. */
static uint8_t*
globaltimbre(uint32_t bank, uint32_t patch)
{
	uint8_t *h;

	for (h = gtl; h[1] != bank || h[0] != patch; h += 6)
		if (h[1] == 0xff)
			return NULL;
	return gtl + (h[2] | h[3]<<8 | h[4]<<16 | (uint32_t)h[5]<<24);
}

/*
 * PlayMidi is midi.c's: the sequence playing stopped and let go, the
 * file's first registered, the timbres it asks for installed, and
 * started. When one is missing, it is not.
 */
void
PlayMidi(unsigned char *xmi)
{
	uint32_t treq;
	uint8_t *t;

	if (!Midi_Driver_Enable)
		return;
	if (hseq != -1) {
		stopseq(hseq);
		releaseseqhandle(hseq);
	}
	hseq = registerseq(xmi, 0, state, NULL);
	if (hseq == -1)
		return;
	while ((treq = synth->request(hseq) & 0xffff) != 0xffff) {
		t = globaltimbre(treq >> 8, treq & 0xff);
		if (t == NULL)
			return;
		synth->install(treq >> 8, treq & 0xff, t);
	}
	startseq(hseq);
}

/* StopMidi is midi.c's. */
void
StopMidi(void)
{
	if (hseq != -1) {
		stopseq(hseq);
		releaseseqhandle(hseq);
		hseq = -1;
	}
}

/* IsMidiPlaying is midi.c's. */
int32_t
IsMidiPlaying(void)
{
	return hseq != -1 && seqstatus(hseq) == 1;
}

/* FadeMidiDown is midi.c's: the first sequence's volume to 0 over nbsec seconds. */
void
FadeMidiDown(int16_t nbsec)
{
	if (hseq != -1)
		setrelvolume(0, 0, 1000 * nbsec);
}

/* VolumeMidi is midi.c's: the first sequence's volume, at once, a percentage of MaxVolume. */
void
VolumeMidi(int16_t volume)
{
	if (hseq != -1)
		setrelvolume(0, (volume*MaxVolume)/100, 0);
}

/* DoLoopMidi is midi.c's: the sequence started again once the first is done. */
void
DoLoopMidi(void)
{
	if (hseq != -1 && seqstatus(0) == 2)
		startseq(hseq);
}

/* The samples at hz by the clock's time t, in ns, t*hz past 64 bits after 103 hours. */
static uint64_t
samplesat(uint64_t t, uint32_t hz)
{
	return t/1000000000 * hz + t%1000000000 * hz/1000000000;
}

/* The chip's frames to the clock's time t, in ns, made and handed on. */
static void
render(uint64_t t)
{
	uint64_t s;

	if (awe) {
		emu8krun(before + samplesat(t, Awehz));
		handon();
		return;
	}
	s = samplesat(t, Oplhz);
	while (samples + nbuf < s) {
		OPL3_Generate(&opl, buf + 2*nbuf);
		if (++nbuf == Nbuf)
			handon();
	}
	handon();
}

/*
 * miditick: each period of AIL's timer the clock has passed, the chip's
 * frames to its start made, then the driver served.
 */
void
miditick(uint64_t ns)
{
	uint64_t n, t;

	if (!Midi_Driver_Enable)
		return;
	for (;;) {
		n = ticks + 1;
		t = n/Pithz * Pitdivisor*1000000000ull + n%Pithz * Pitdivisor*1000000000ull / Pithz;
		if (t > ns)
			break;
		render(t);
		servedriver();
		ticks++;
	}
}
