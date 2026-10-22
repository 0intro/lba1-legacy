#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"
#include "suite.h"

/*
 * fmoracle: the CD's XMIDI driver for the OPL3, A32SP2FM.DLL, against
 * sys/xmidi.c and sys/yamaha.c, its translation.
 *
 *	fmoracle [-s seconds] [-c blocks.tsv] a32sp2fm.elf midi_sb.hqr sample.opl [scenario...]
 *
 * a32sp2fm.elf is the driver as tools/le2elf.py makes it, mapped and
 * called through its index as AIL/32 called it (dll.c), its port I/O
 * done for it: a write to the OPL3's address port (220h, 222h for the
 * second array) then to its data port being a register write, and
 * every status read 0.
 *
 * Both play every tune of midi_sb.hqr as lib_midi's PlayMidi starts one
 * (sequence 0 registered, the timbres it asks for installed from the
 * GTL, started, its volume set to 100), served 120 times a second for
 * a minute (or the seconds given), restarted when done as DoLoopMidi
 * does, its volume changed at once and faded as the game does. The
 * register writes, interval by interval, must be the same. It says too
 * whether the driver's note queue ever filled its last entry.
 *
 * Before, the scenarios of suite.c, all or those named, run on the DLL
 * and on the translation in two processes, streamed and compared: the
 * register writes, however made, and the accesses to the mixer's ports,
 * the card's ports modelled for both as the port models them
 * (sys/oplio.c), and in the detection every port access. At each step,
 * the synthesizer's and the sequencer's variables and each registered
 * sequence's state table, the DLL's bytes (from 0x106eb and 0x12c7b,
 * and the tables the sequencer lists) against the C's laid out alike
 * (yamahastate, xmidistate, xmidiseqstate). With -c, the blocks of
 * blocks.tsv, each function's basic blocks as Ghidra lists them, each
 * get an INT3 in the DLL's runs, and those left at 0 runs are listed.
 *
 *	sequencer, sequences, corners, queue
 *		suite.c's, the timbres the sequences ask for installed from
 *		sample.opl.
 *	lifecycle
 *		the card looked for at every base, and failed as a card
 *		whose timer never raises its flag, whose status is stuck at
 *		0xe0 and whose mixer does not keep its bits, then as one
 *		whose status reads, or whose mixer reads, have a bit set,
 *		each in turn, the detection's port accesses streamed. Then
 *		set up, shut down, set up and shut down again.
 *	edges	the pedal and the timbre and voice protections at the edges
 *		of 64 and past 127: notes released under the pedal and held
 *		by it as the value comes, a channel's voices all taken and a
 *		note from another. A timbre of 300 bytes installed and pushed
 *		out of the cache. Levels at full scale, the pan at its
 *		thresholds, the lowest notes, values past a byte. Timbres of
 *		bank and number past a byte, drum timbres of keys 0 and 127,
 *		a waiting slot's timbre, an entry let go above the next one
 *		pushed out, a timbre of length 0, each pushed out of the
 *		cache. Three waiting notes outranking the voices at once, and
 *		a four-operator note taking a two-operator voice, whose pair's
 *		other voice the driver looks up by slot.
 *	timbres	every timbre of sample.opl installed in turn, the cache
 *		filled past full, some protected, notes on each.
 *	random	a hundred thousand messages drawn at random through
 *		send_cv_msg, every timbre installed first.
 */

enum
{
	Base = 0x220,
};

typedef struct Regwrite Regwrite;

struct Regwrite
{
	uint32_t	tick;
	uint16_t	reg;
	uint8_t	val;
};

typedef struct Log Log;

struct Log
{
	Regwrite	*w;
	int	n;
	int	max;
};

typedef void	(*Finit)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void	(*Fvoid)(void);
typedef uint32_t	(*Fh)(uint32_t);
typedef uint32_t	(*Fhs)(uint32_t, uint32_t);
typedef void	(*Fcache)(uint32_t, void*, uint32_t);
typedef uint32_t	(*Freg)(uint32_t, void*, uint32_t, void*, void*);
typedef void	(*Finst)(uint32_t, uint32_t, uint32_t, void*);
typedef void	(*Fvol)(uint32_t, uint32_t, uint32_t, uint32_t);

typedef struct Driver Driver;

/* The calls lib_midi's midi.c makes, on either driver. */
struct Driver
{
	void	(*init)(void);
	void	(*serve)(void);
	uint32_t	(*statesize)(void);
	uint32_t	(*cachesize)(void);
	void	(*definecache)(void*, uint32_t);
	int32_t	(*registerseq)(uint8_t*, void*);
	void	(*releaseseq)(int32_t);
	int32_t	(*request)(int32_t);
	void	(*install)(uint32_t, uint32_t, uint8_t*);
	void	(*start)(int32_t);
	void	(*stop)(int32_t);
	int32_t	(*status)(int32_t);
	void	(*setvolume)(int32_t, int32_t, uint32_t);
};

void	Expand(void*, void*, uint32_t);

static Log logs[2];
static Log *cur;
static int streaming;
static int detecting;
static int fault;
static uint8_t tcache[3584];
static uint8_t tstate[4096];	/* a sequence's state, for edges */
static uint8_t tvfx[32];
static uint32_t tick;
static uint32_t addr[2];
static uint8_t *gtl;
static uint32_t nticks = 120 * 60;

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "fmoracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

static void
logwrite(uint32_t reg, uint32_t val)
{
	if (cur->n == cur->max) {
		cur->max = cur->max ? 2*cur->max : 1<<16;
		cur->w = realloc(cur->w, cur->max * sizeof cur->w[0]);
		if (cur->w == NULL)
			fatal("out of memory");
	}
	cur->w[cur->n].tick = tick;
	cur->w[cur->n].reg = reg;
	cur->w[cur->n].val = val;
	cur->n++;
}

/* The register writes: logged for the tunes, streamed for the scenarios. */
void
oplwrite(uint32_t reg, uint32_t val)
{
	if (streaming)
		emitio(0x01000000 | reg, val, 0);
	else
		logwrite(reg, val);
}

/* The card's ports, as both drivers see them in the scenarios: the mixer's accesses streamed. */
void
oplout(uint32_t port, uint32_t val)
{
	if (port == Base+4 || port == Base+5)
		emitio(0x02000000 | port, val & 0xff, 0);
	else if (detecting)
		emitio(0x05000000 | port, val & 0xff, 0);
	oplportout(port, val);
}

uint32_t
oplin(uint32_t port)
{
	uint32_t v;

	v = oplportin(port);
	switch (fault) {
	case 1:
		if (port == Base+8)
			v = 0;
		break;
	case 2:
		if (port == Base+8)
			v = 0xe0;
		break;
	case 3:
		if (port == Base+5)
			v = 0;
		break;
	}
	/* then a bit set in every status read, then in every mixer read */
	if (fault >= 4 && fault < 12 && (port == Base || port == Base+8))
		v |= 1 << (fault - 4);
	if (fault >= 12 && fault < 20 && port == Base+5)
		v |= 1 << (fault - 12);
	if (port == Base+5)
		emitio(0x03000000 | port, v, 0);
	else if (detecting)
		emitio(0x04000000 | port, v, 0);
	return v;
}

/*
 * The driver's detection at base, its status reads streamed too: the C
 * makes them as the DLL does there, where its register writes elsewhere
 * leave out the DLL's reads of the status between them. Those take time
 * on the ports' clock, so the card's model is reset first, its timers
 * then where they were for both.
 */
static uint32_t
detectat(uint32_t base)
{
	uint32_t r;

	oplioreset(Base);
	detecting = 1;
	r = api(101, 4, base, -1, -1, -1);
	detecting = 0;
	return r;
}

/* The DLL's port I/O in the scenarios, a byte at a time. */
static void
soutp(uint32_t port, uint32_t v, int w)
{
	int i;

	for (i = 0; i < w; i++)
		oplout(port + i, v >> 8*i & 0xff);
}

static uint32_t
sinp(uint32_t port, int w)
{
	uint32_t v;
	int i;

	v = 0;
	for (i = 0; i < w; i++)
		v |= oplin(port + i) << 8*i;
	return v;
}

/* The CD's driver's port I/O: register writes at the OPL3's ports, every read 0. */
static void
out(uint32_t port, uint32_t v, int w)
{
	USED(w);
	v &= 0xff;
	switch (port) {
	case Base:
		addr[0] = v;
		break;
	case Base+2:
		addr[1] = v;
		break;
	case Base+1:
		logwrite(addr[0], v);
		break;
	case Base+3:
		logwrite(0x100 | addr[1], v);
		break;
	}
}

static uint32_t
in(uint32_t port, int w)
{
	USED(port);
	USED(w);
	return 0;
}

/* The CD's driver. */
static void dinit(void) { ((Finit)(uintptr_t)dllproc(102))(0, Base, -1, -1, -1); }
static void dserve(void) { ((Fvoid)(uintptr_t)dllproc(103))(); }
static uint32_t dstatesize(void) { return ((Fh)(uintptr_t)dllproc(150))(0); }
static uint32_t dcachesize(void) { return ((Fh)(uintptr_t)dllproc(153))(0); }
static void ddefinecache(void *a, uint32_t n) { ((Fcache)(uintptr_t)dllproc(154))(0, a, n); }
static int32_t dregisterseq(uint8_t *x, void *st) { return ((Freg)(uintptr_t)dllproc(151))(0, x, 0, st, NULL); }
static void dreleaseseq(int32_t h) { ((Fhs)(uintptr_t)dllproc(152))(0, h); }
static int32_t drequest(int32_t h) { return ((Fhs)(uintptr_t)dllproc(155))(0, h); }
static void dinstall(uint32_t b, uint32_t n, uint8_t *t) { ((Finst)(uintptr_t)dllproc(156))(0, b, n, t); }
static void dstart(int32_t h) { ((Fhs)(uintptr_t)dllproc(170))(0, h); }
static void dstop(int32_t h) { ((Fhs)(uintptr_t)dllproc(171))(0, h); }
static int32_t dstatus(int32_t h) { return ((Fhs)(uintptr_t)dllproc(174))(0, h); }
static void dsetvolume(int32_t h, int32_t v, uint32_t ms) { ((Fvol)(uintptr_t)dllproc(177))(0, h, v, ms); }

/* The translation. */
static void tinit(void) { initdriver(&opl3, Base, -1, -1, -1); }
static uint32_t tcachesize(void) { return opl3.cachesize(); }
static void tdefinecache(void *a, uint32_t n) { opl3.definecache(a, n); }
static int32_t tregisterseq(uint8_t *x, void *st) { return registerseq(x, 0, st, NULL); }
static int32_t trequest(int32_t h) { return opl3.request(h); }
static void tinstall(uint32_t b, uint32_t n, uint8_t *t) { opl3.install(b, n, t); }

static Driver drivers[2] = {
	{dinit, dserve, dstatesize, dcachesize, ddefinecache, dregisterseq, dreleaseseq,
		drequest, dinstall, dstart, dstop, dstatus, dsetvolume},
	{tinit, servedriver, statesize, tcachesize, tdefinecache, tregisterseq, releaseseqhandle,
		trequest, tinstall, startseq, stopseq, seqstatus, setrelvolume},
};

/* load_global_timbre, midi.c's: the GTL entry of the bank and patch, or NULL. */
static uint8_t*
timbre(uint32_t bank, uint32_t patch)
{
	uint8_t *h;

	for (h = gtl; h[1] != bank || h[0] != patch; h += 6)
		if (h[1] == 0xff)
			return NULL;
	return gtl + get32(h+2);
}

/*
 * One driver plays every tune: PlayMidi then VolumeMidi(100) for each,
 * DoLoopMidi after each interval, and partway VolumeMidi(70), a fade
 * down over 2 s as FadeMidiDown(2) and VolumeMidi(100) again.
 */
static void
play(Driver *d, uint8_t **tunes, int ntunes, int ref)
{
	uint8_t *state, *cache;
	uint32_t treq;
	int32_t h;
	int t, full;

	tick = 0;
	d->init();
	cache = calloc(1, d->cachesize());
	state = calloc(1, d->statesize());
	if (cache == NULL || state == NULL)
		fatal("out of memory");
	d->definecache(cache, d->cachesize());
	full = 0;
	h = -1;
	for (t = 0; t < ntunes; t++) {
		if (tunes[t] == NULL)
			continue;
		if (h != -1) {
			d->stop(h);
			d->releaseseq(h);
		}
		h = d->registerseq(tunes[t], state);
		if (h == -1)
			continue;
		while ((treq = d->request(h) & 0xffff) != 0xffff)
			if (timbre(treq>>8, treq & 0xff) == NULL)
				break;
			else
				d->install(treq>>8, treq & 0xff, timbre(treq>>8, treq & 0xff));
		if (treq != 0xffff)
			continue;
		d->start(h);
		d->setvolume(0, 100, 0);
		for (tick = t * nticks; tick < (uint32_t)(t+1)*nticks; tick++) {
			d->serve();
			if (d->status(0) == 2)
				d->start(h);
			switch (tick - t*nticks) {
			case 20*120:
				d->setvolume(0, 70, 0);
				break;
			case 40*120:
				d->setvolume(0, 0, 2000);
				break;
			case 45*120:
				d->setvolume(0, 100, 0);
				break;
			}
			/* the CD's driver's note queue: note_chan[31] at 375 */
			if (ref && state[375] != 0xff)
				full++;
		}
	}
	if (h != -1) {
		d->stop(h);
		d->releaseseq(h);
	}
	if (ref)
		printf("fmoracle: the driver's note queue had its last entry in use in %d intervals\n", full);
}

/* timbreof: the GTL's timbre of the bank and patch, or NULL. */
uint8_t*
timbreof(uint32_t bank, uint32_t num)
{
	return timbre(bank, num);
}

/*
 * statecrc: the CRC of the driver's writable data, the DLL's synthesizer
 * and sequencer variables or the C's laid out alike (yamahastate,
 * xmidistate), then each registered sequence's state table.
 */
uint32_t
statecrc(void)
{
	uint8_t b[0xed4 + 0x22b];

	if (indll) {
		memcpy(b, (void*)(uintptr_t)0x106eb, 0xed4);
		memcpy(b + 0xed4, (void*)(uintptr_t)0x12c7b, 0x22b);
	} else {
		yamahastate(b);
		xmidistate(b + 0xed4);
	}
	return seqcrc(crc(0, b, sizeof b), b + 0xed4);
}

/* runinit: a run's ports, and on the DLL its I/O. */
void
runinit(void)
{
	streaming = 1;
	oplioreset(Base);
	if (indll)
		dllio(soutp, sinp);
}

/* initmidi: lib_midi's InitMidi, the driver described, the card found, set up, its cache given. */
void
initmidi(void)
{
	uint32_t r, n;

	r = api(100, 0);
	step("describe", r != 0);
	step("detect", detectat(Base));
	api(102, 4, Base, -1, -1, -1);
	step("init", 0);
	api(150, 0);
	step("statesize", 0);
	n = api(153, 0);
	step("cachesize", n);
	if (n > sizeof tcache)
		fatal("the driver wants a cache of %u bytes", n);
	api(154, 2, (uint32_t)(uintptr_t)tcache, n);
	step("define cache", 0);
}

/* interval: the service. */
void
interval(void)
{
	api(103, 0);
}

/* A message through send_cv_msg, then a step of result 0, the API's call being void. */
static void
msg(uint32_t st, uint32_t d1, uint32_t d2)
{
	api(186, 3, st, d1, d2);
	step("cv %02x %02x %02x", 0, st, d1, d2);
}

/* Channel ch on program p of bank 0, its timbre installed first. */
static void
program(uint32_t ch, uint32_t p)
{
	uint8_t *t;

	t = timbre(0, p);
	if (t == NULL)
		fatal("no timbre for program %u", p);
	api(156, 3, 0, p, (uint32_t)(uintptr_t)t);
	step("install 00 %02x", 0, p);
	msg(0xb0|ch, 114, 0);
	msg(0xc0|ch, p, 0);
}

/* Every timbre of the GTL installed, in its order. */
static void
alltimbres(void)
{
	uint8_t *h;

	for (h = gtl; h[1] != 0xff; h += 6) {
		api(156, 3, h[1], h[0], (uint32_t)(uintptr_t)(gtl + get32(h+2)));
		step("install %02x %02x", 0, h[1], h[0]);
	}
}

static void
lifecycle(void)
{
	uint32_t b;
	int f;

	step("describe", api(100, 0) != 0);
	for (b = 0x200; b <= 0x2a0; b += 0x20)
		step("detect at %x", detectat(b), b);
	for (f = 1; f < 20; f++) {
		fault = f;
		step("detect, fault %d", detectat(Base), f);
		fault = 0;
	}
	for (f = 0; f < 2; f++) {
		step("detect", detectat(Base));
		api(102, 4, Base, -1, -1, -1);
		step("init", 0);
		api(154, 2, (uint32_t)(uintptr_t)tcache, sizeof tcache);
		alltimbres();
		msg(0xc1, 0, 0);
		msg(0x91, 0x3c, 0x64);
		msg(0xc9, 0, 0);
		msg(0x99, 0x26, 0x64);
		interval();
		api(104, 1, (uint32_t)(uintptr_t)"");
		step("shutdown", 0);
	}
}

static void
timbreset(void)
{
	uint8_t *h;
	uint32_t i, k;

	initmidi();
	for (k = 0; k < 3; k++) {
		i = 0;
		for (h = gtl; h[1] != 0xff; h += 6) {
			api(156, 3, h[1], h[0], (uint32_t)(uintptr_t)(gtl + get32(h+2)));
			step("install %02x %02x", 0, h[1], h[0]);
			if (k == 1 && i % 7 == 0) {
				api(157, 2, h[1], h[0]);
				step("protect", 0);
			}
			if (k == 2 && i % 5 == 0) {
				api(158, 2, h[1], h[0]);
				step("unprotect", 0);
			}
			step("status", api(159, 2, h[1], h[0]));
			if (h[1] == 0) {
				msg(0xc1 + i % 8, h[0], 0);
				msg(0x91 + i % 8, 0x30 + i % 40, 0x60);
				interval();
				msg(0x81 + i % 8, 0x30 + i % 40, 0);
			}
			i++;
		}
	}
	msg(0xb1, 114, 1);
	msg(0xc1, 3, 0);
	msg(0x91, 0x40, 0x40);
	interval();
	/* all protected and unprotected at once */
	api(157, 2, 0xff, 0xff);
	step("protect all", 0);
	api(158, 2, 0xff, 0xff);
	step("unprotect all", 0);
	/*
	 * timbres put out while their notes sound, those after them moved:
	 * one note held from the first, the others let go some later
	 */
	msg(0xb1, 114, 0);
	api(156, 3, gtl[1], gtl[0], (uint32_t)(uintptr_t)(gtl + get32(gtl+2)));
	msg(0xc1, gtl[0], 0);
	msg(0x91, 0x3c, 0x60);
	for (k = 0; k < 2; k++) {
		i = 0;
		for (h = gtl; h[1] != 0xff; h += 6) {
			api(156, 3, h[1], h[0], (uint32_t)(uintptr_t)(gtl + get32(h+2)));
			step("install %02x %02x", 0, h[1], h[0]);
			if (h[1] == 0) {
				msg(0xc2 + i % 7, h[0], 0);
				msg(0x92 + i % 7, 0x30 + i % 40, 0x60);
				if (i >= 7)
					msg(0x82 + (i - 7) % 7, 0x30 + (i - 7) % 40, 0);
				i++;
			}
			if (h[1] == 127) {
				msg(0x99, h[0], 0x60);
				msg(0x89, h[0], 0);
			}
			if (i % 4 == 0)
				interval();
		}
	}
	/* more timbres than the cache has entries, the smallest */
	for (h = gtl; h[1] != 0xff && get16(gtl + get32(h+2)) != 14; h += 6)
		;
	if (h[1] == 0xff)
		fatal("no timbre of 14 bytes");
	for (i = 0; i < 200; i++) {
		api(156, 3, 2 + i/128, i % 128, (uint32_t)(uintptr_t)(gtl + get32(h+2)));
		step("install %02x %02x", 0, 2 + i/128, i % 128);
	}
	for (i = 0; i < 16; i++)
		msg(0xb0|i, 123, 0);
	interval();
	/* the pedal held through note offs, then let go */
	program(2, 0);
	msg(0xb2, 64, 0x7f);
	for (i = 0; i < 6; i++)
		msg(0x92, 0x30 + 4*i, 0x60);
	for (i = 0; i < 6; i++)
		msg(0x82, 0x30 + 4*i, 0);
	interval();
	msg(0xb2, 64, 0);
	interval();
	/*
	 * more notes than the slots: the six four-operator voices
	 * protected, then waiting notes, which keep their slots
	 */
	program(2, 0);
	msg(0xb3, 114, 0);
	msg(0xc3, 0, 0);
	msg(0xb2, 112, 0x7f);
	for (i = 0; i < 6; i++)
		msg(0x92, 0x30 + i, 0x60);
	for (i = 0; i < 16; i++)
		msg(0x93, 0x40 + i, 0x60);
	msg(0xb3, 7, 0x50);
	interval();
	msg(0xb2, 112, 0);
	for (i = 0; i < 16; i++)
		msg(0xb0|i, 123, 0);
	interval();
	/* a timbre of 20 bytes, between BNK's 14 and OPL3BNK's 25: a TVFX's, dead in this build */
	for (h = gtl; h[1] != 0xff && get16(gtl + get32(h+2)) != 25; h += 6)
		;
	if (h[1] == 0xff)
		fatal("no timbre of 25 bytes");
	memcpy(tvfx, gtl + get32(h+2), 25);
	tvfx[0] = 20;
	api(156, 3, 5, 0, (uint32_t)(uintptr_t)tvfx);
	step("install a TVFX timbre", 0);
	msg(0xb2, 114, 5);
	msg(0xc2, 0, 0);
	msg(0x92, 0x40, 0x60);
	interval();
	msg(0x82, 0x40, 0);
	msg(0xb2, 114, 0);
	interval();
}

static void
randoms(void)
{
	static uint32_t ctrls[] = {1, 7, 10, 11, 64, 112, 113, 114, 121, 123};
	uint32_t i, ch, c, v;

	initmidi();
	alltimbres();
	seed = 1;
	for (i = 0; i < 100000; i++) {
		ch = rnd(16);
		switch (rnd(10)) {
		case 0:
		case 1:
		case 2:
		case 3:
			msg(0x90|ch, rnd(0x80), rnd(8) == 0 ? rnd(0x100) : 1 + rnd(0x7f));
			break;
		case 4:
		case 5:
			msg(0x80|ch, rnd(0x80), rnd(0x80));
			break;
		case 6:
			c = rnd(4) == 0 ? rnd(0x100) : ctrls[rnd(nelem(ctrls))];
			v = rnd(0x100);
			msg(0xb0|ch, c, v);
			break;
		case 7:
			msg(0xc0|ch, rnd(0x80), 0);
			break;
		case 8:
			msg(0xe0|ch, rnd(0x100), rnd(0x100));
			break;
		default:
			msg(0xa0 + 0x30*rnd(2) + ch, rnd(0x100), rnd(0x100));
			break;
		}
		if (rnd(4) == 0)
			interval();
	}
}

/*
 * The levels and pitches at their edges: an additive timbre's
 * modulator at full volume, expression and velocity, the pan at each
 * threshold, the lowest notes, whose octave wraps, and a controller and
 * a velocity past a byte through send_cv_msg.
 */
static void
levels(void)
{
	initmidi();
	program(3, 15);
	msg(0xb3, 7, 127);
	msg(0xb3, 11, 127);
	msg(0x93, 0x40, 127);
	interval();
	msg(0xb3, 10, 27);
	msg(0xb3, 10, 28);
	msg(0xb3, 10, 100);
	msg(0xb3, 10, 99);
	msg(0x93, 0, 0x60);
	msg(0x93, 12, 0x60);
	interval();
	msg(0x83, 0, 0);
	msg(0x83, 12, 0);
	msg(0x83, 0x40, 0);
	msg(0xb3, 0x107, 0x30);
	msg(0x93, 0x45, 0x100);
	interval();
	msg(0xb3, 123, 0);
}

/*
 * The cache at its corners, from empty each time. A timbre of bank and
 * number past a byte, given to the channels whose program asks for it,
 * channel 1 among them, and the banks set up again. Drum timbres of
 * keys 0 and 127 pushed out. A slot waiting for a voice, its timbre
 * pushed out. An entry let go above the one pushed out next. A timbre
 * of length 0 where the one pushed out began.
 */
static void
cachecorners(void)
{
	static uint8_t twice[50], none[25];
	uint8_t *t, *d;
	uint32_t i;

	t = timbre(0, 15);
	d = timbre(127, 35);
	memcpy(twice, t, 25);
	twice[0] = sizeof twice;
	memcpy(none, t, 25);
	none[0] = 0;
	initmidi();
	msg(0xb4, 114, 3);
	msg(0xc4, 0x37, 0);
	msg(0xb0, 114, 3);
	msg(0xc0, 0x37, 0);
	api(156, 3, 0x103, 0x137, (uint32_t)(uintptr_t)t);
	step("install 103 137", 0);
	initmidi();
	api(156, 3, 127, 0, (uint32_t)(uintptr_t)d);
	api(156, 3, 127, 127, (uint32_t)(uintptr_t)d);
	step("install drums 0 and 127", 0);
	msg(0x99, 0, 0x60);
	msg(0x99, 127, 0x60);
	interval();
	msg(0x89, 0, 0);
	msg(0x89, 127, 0);
	alltimbres();
	/* channel 3's note left waiting, behind channel 6's protected notes */
	initmidi();
	api(156, 3, 3, 1, (uint32_t)(uintptr_t)t);
	api(156, 3, 3, 2, (uint32_t)(uintptr_t)t);
	step("install 3 1, 3 2", 0);
	msg(0xb2, 114, 3);
	msg(0xc2, 1, 0);
	msg(0xb5, 114, 3);
	msg(0xc5, 2, 0);
	msg(0xb5, 112, 0x7f);
	msg(0x92, 0x40, 0x60);
	for (i = 0; i < 6; i++)
		msg(0x95, 0x30 + i, 0x60);
	msg(0x92, 0x41, 0x60);
	api(157, 2, 3, 2);
	step("protect 3 2", 0);
	/* 3 1 the least used alone: the first pushed out, before the walk's steps spoil the slots' addresses */
	msg(0x95, 0x48, 0x60);
	msg(0x85, 0x48, 0);
	alltimbres();
	/* the entry of 3 2 let go, then 3 1's below it */
	initmidi();
	api(156, 3, 3, 1, (uint32_t)(uintptr_t)t);
	api(156, 3, 3, 2, (uint32_t)(uintptr_t)t);
	for (i = 0; i < 141; i++) {
		api(156, 3, 4 + i/128, i % 128, (uint32_t)(uintptr_t)t);
		api(157, 2, 4 + i/128, i % 128);
	}
	step("cache filled, protected", 0);
	msg(0xb2, 114, 3);
	msg(0xc2, 1, 0);
	msg(0x92, 0x40, 0x60);
	interval();
	msg(0x82, 0x40, 0);
	api(156, 3, 6, 0, (uint32_t)(uintptr_t)twice);
	step("install 50 bytes", 0);
	step("status 3 2", api(159, 2, 3, 2));
	/* a timbre of length 0, then one at its offset, pushed out */
	initmidi();
	api(156, 3, 7, 0, (uint32_t)(uintptr_t)none);
	api(157, 2, 7, 0);
	api(156, 3, 7, 1, (uint32_t)(uintptr_t)t);
	for (i = 0; i < 142; i++) {
		api(156, 3, 4 + i/128, i % 128, (uint32_t)(uintptr_t)t);
		api(157, 2, 4 + i/128, i % 128);
	}
	step("cache filled, protected", 0);
	api(156, 3, 6, 1, (uint32_t)(uintptr_t)t);
	step("install 6 1", 0);
	step("status 7 0", api(159, 2, 7, 0));
}

/*
 * Voices taken by rank: channel 3's notes left waiting behind channel
 * 6's protected ones, which are then unprotected. A third note finds
 * all three ranking above every voice.
 */
static void
steals(void)
{
	uint8_t *t;
	uint32_t i;

	initmidi();
	t = timbre(0, 15);
	api(156, 3, 3, 1, (uint32_t)(uintptr_t)t);
	api(156, 3, 3, 2, (uint32_t)(uintptr_t)t);
	step("install 3 1, 3 2", 0);
	msg(0xb2, 114, 3);
	msg(0xc2, 1, 0);
	msg(0xb5, 114, 3);
	msg(0xc5, 2, 0);
	msg(0xb5, 112, 0x7f);
	for (i = 0; i < 6; i++)
		msg(0x95, 0x30 + i, 0x60);
	msg(0x92, 0x40, 0x60);
	msg(0x92, 0x41, 0x60);
	msg(0xb5, 112, 0);
	msg(0x92, 0x42, 0x60);
	interval();
	for (i = 0; i < 16; i++)
		msg(0xb0|i, 123, 0);
	interval();
	/*
	 * a two-operator voice on a four-operator pair's first, lowest of
	 * all, given to channel 3 in slot 3 when channel 6's protected
	 * notes have voices 0 to 11. A four-operator note takes it, and the
	 * other voice of the pair is looked up by slot 3's number: voice 0,
	 * slot 0's
	 */
	initmidi();
	api(156, 3, 3, 1, (uint32_t)(uintptr_t)timbre(127, 31));
	step("install 3 1", 0);
	program(4, 15);
	msg(0xb5, 114, 3);
	msg(0xc5, 1, 0);
	msg(0xb5, 112, 0x7f);
	msg(0xb2, 114, 3);
	msg(0xc2, 1, 0);
	for (i = 0; i < 10; i++)
		msg(0x95, 0x30 + i, 0x60);
	msg(0x85, 0x33, 0);
	msg(0x92, 0x40, 0x60);
	msg(0x95, 0x3a, 0x60);
	msg(0x94, 0x48, 0x60);
	interval();
	for (i = 0; i < 16; i++)
		msg(0xb0|i, 123, 0);
	interval();
}

/*
 * edges: the pedal and the two protections at the edges of 64 and past
 * 127, a note released under each, and a timbre of 300 bytes, its
 * length's high byte, installed and pushed out of the cache. Then
 * levels, cachecorners and steals.
 */
static void
edges(void)
{
	static uint32_t vs[] = {0x3f, 0x40, 0x41, 0x7f, 0x80, 0xc0};
	static uint8_t big[300];
	uint32_t i, c;
	uint8_t *h, *t;

	initmidi();
	/* a request for handle -1 while the first sequence waits, then timbres outside the cache protected */
	c = api(151, 4, (uint32_t)(uintptr_t)xmidi(0), 0, (uint32_t)(uintptr_t)tstate, 0);
	step("register", c);
	step("request -1", api(155, 1, -1));
	api(152, 1, c);
	api(157, 2, 0x7d, 0x7d);
	api(158, 2, 0x7d, 0x7d);
	step("absent protected", 0);
	alltimbres();
	program(0, 0x20);
	program(1, 0x10);
	program(2, 0x10);
	for (i = 0; i < nelem(vs); i++) {
		/* a note released under the pedal at the value, then one held by it as the value comes */
		msg(0x91, 0x3c, 0x64);
		msg(0xb1, 64, vs[i]);
		msg(0x81, 0x3c, 0);
		interval();
		msg(0xb1, 64, 0);
		msg(0xb1, 64, 0x7f);
		msg(0x91, 0x3e, 0x64);
		msg(0x81, 0x3e, 0);
		msg(0xb1, 64, vs[i]);
		interval();
		msg(0xb1, 64, 0);
		/* the channel's voices protected at the value, all taken, and a note from another */
		msg(0xb1, 112, vs[i]);
		for (c = 0; c < 6; c++)
			msg(0x91, 0x30 + c, 0x64);
		msg(0x92, 0x48, 0x64);
		interval();
		msg(0xb1, 123, 0);
		msg(0xb2, 123, 0);
		msg(0xb1, 112, 0);
		/* the timbre protected at the value */
		msg(0xb1, 113, vs[i]);
		msg(0x91, 0x40, 0x64);
		interval();
		msg(0x81, 0x40, 0);
		msg(0xb1, 113, 0);
	}
	t = timbre(0, 0x10);
	memcpy(big, t, t[0] | t[1]<<8);
	big[0] = sizeof big & 0xff;
	big[1] = sizeof big >> 8;
	api(156, 3, 0x7e, 0x7e, (uint32_t)(uintptr_t)big);
	step("install 300 bytes", 0);
	for (h = gtl; h[1] != 0xff; h += 6) {
		api(156, 3, h[1], h[0], (uint32_t)(uintptr_t)(gtl + get32(h+2)));
		step("install %02x %02x", 0, h[1], h[0]);
	}
	api(156, 3, 0x7e, 0x7e, (uint32_t)(uintptr_t)big);
	step("install 300 bytes again", 0);
	step("status", api(159, 2, 0x7e, 0x7e));
	levels();
	cachecorners();
	steals();
}

static Scenario scenarios[] = {
	{"sequencer", sequencer, 1},
	{"sequences", sequences, 1},
	{"queue", queue, 1},
	{"corners", corners, 1},
	{"lifecycle", lifecycle, 1},
	{"timbres", timbreset, 1},
	{"edges", edges, 1},
	{"random", randoms, 1},
};

int
main(int argc, char *argv[])
{
	uint8_t **tunes;
	char *cover;
	long n;
	int ntunes, i, k, bad;
	Regwrite *a, *b;

	cover = NULL;
	while (argc > 2 && argv[1][0] == '-') {
		if (strcmp(argv[1], "-s") == 0)
			nticks = 120 * strtoul(argv[2], NULL, 10);
		else if (strcmp(argv[1], "-c") == 0)
			cover = argv[2];
		else
			break;
		argc -= 2;
		argv += 2;
	}
	if (argc < 4) {
		fprintf(stderr, "usage: fmoracle [-s seconds] [-c blocks.tsv] a32sp2fm.elf midi_sb.hqr sample.opl [scenario...]\n");
		return 2;
	}
	asked("fmoracle", argv+4, argc-4, scenarios, nelem(scenarios));
	crcinit();
	synthc = &opl3;
	for (i = 0; i < Nfiles; i++)
		xmidi(i);
	dllload(argv[1]);
	if (cover != NULL)
		readblocks(cover);
	ntunes = hqr(argv[2], &tunes);
	gtl = readfile(argv[3], &n);
	bad = 0;
	for (i = 0; i < (int)nelem(scenarios); i++) {
		if (argc > 4) {
			for (k = 4; k < argc; k++)
				if (strcmp(argv[k], scenarios[i].name) == 0)
					break;
			if (k == argc)
				continue;
		}
		bad |= scenario("fmoracle", &scenarios[i]);
	}
	if (argc > 4) {
		if (cover != NULL)
			coverage("fmoracle");
		return bad;
	}
	dllio(out, in);
	if (cover != NULL)
		coverblocks();
	for (i = 0; i < 2; i++) {
		cur = &logs[i];
		play(&drivers[i], tunes, ntunes, i == 0);
	}
	for (i = 0; i < logs[0].n && i < logs[1].n; i++) {
		a = &logs[0].w[i];
		b = &logs[1].w[i];
		if (a->tick != b->tick || a->reg != b->reg || a->val != b->val) {
			printf("fmoracle: write %d differs: driver tick %u reg %03x val %02x, ours tick %u reg %03x val %02x\n",
				i, a->tick, a->reg, a->val, b->tick, b->reg, b->val);
			return 1;
		}
	}
	if (logs[0].n != logs[1].n) {
		printf("fmoracle: driver wrote %d registers, ours %d\n", logs[0].n, logs[1].n);
		return 1;
	}
	printf("fmoracle: %d tunes, %d register writes, the same\n", ntunes, logs[0].n);
	if (cover != NULL)
		coverage("fmoracle");
	return bad;
}
