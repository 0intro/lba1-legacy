#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "dat.h"
#include "fns.h"
#include "nuked/opl3.h"

/*
 * The music, as lib386/lib_midi/midi.c played it through AIL/32: the
 * XMIDI driver of the OPL3, A32SP2FM.DLL's synthesizer (yamaha.c) under
 * its sequencer (xmidi.c), under the game's calls (PlayMidi and its
 * kin), the timbres it asks for from the Global Timbre Library,
 * "SAMPLE." and the driver's suffix, loaded whole. AIL/32 served the
 * driver from the timer it set to the driver's 120 Hz, a PIT divisor of
 * 9943. Here the clock serves it as it passes each of those periods
 * (miditick).
 *
 * The OPL3 is Nuked OPL3 at its 49716 Hz, given the registers the
 * driver writes, and its ports are modelled for the driver's detection
 * alone (oplio.c): the timers' flags in its status, the card's mixer.
 * The chip's samples go to their own stream, a timer period's at a
 * time, at the mixer's MIDI and master volumes. The driver's memory is
 * the C library's, as midi.c's was, not the game's.
 */

enum
{
	Pitdivisor = 9943,
	Pithz = 1193182,
	Oplhz = 49716,
	Nbuf = 4096,	/* frames handed on at most at once */
};

int16_t	Midi_Driver_Enable;
int32_t	MaxVolume = 100;

static Synth *synth;
static Drvrdesc *desc;
static opl3_chip opl;
static uint8_t *gtl;
static uint8_t *state;
static uint8_t *cache;
static int32_t hseq = -1;
static uint64_t ticks;		/* served */
static uint64_t samples;	/* handed on */
static int16_t buf[2*Nbuf];
static int nbuf;

/* The frames made since last handed on, to the stream and the record. */
static void
handon(void)
{
	if (nbuf == 0)
		return;
	samples += nbuf;
	sdlmidi(buf, nbuf, Oplhz);
	summedmidi(samples, buf, nbuf);
	nbuf = 0;
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

/*
 * midiinit is midi.c's InitMidiDLL and InitMidi, AIL/32's calls as they
 * made them: the driver set up for its card at MidiBase (MidiIRQ and
 * MidiDMA read by name, kept in the descriptor), looked for and set up,
 * its timbre cache given it if it wants one, its state made, the GTL
 * loaded: 1, or 0 with the reason said.
 */
int
midiinit(void)
{
	char gtlname[16];
	int32_t base, irq, dma;
	uint32_t n;

	synth = &opl3;
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
	OPL3_Reset(&opl, Oplhz);
	oplioreset(desc->io);
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
	return 1;
}

/* ClearMidi is midi.c's: AIL/32 shut down, the driver signed off. */
void
ClearMidi(void)
{
	if (!Midi_Driver_Enable)
		return;
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
