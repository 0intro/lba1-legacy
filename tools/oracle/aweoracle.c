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
 * aweoracle: the CD's XMIDI driver for the Sound Blaster AWE32,
 * A32GFX.DLL, against its decompilation: sys/xmidi.c (its sequencer),
 * sys/awe.c (its glue and Creative's library) and sys/awedata.c (their
 * data).
 *
 *	aweoracle [-s seconds] [-c blocks.tsv] [-w dir] [-l log] a32gfx.elf midi_mi.hqr midi_sb.hqr awe32.raw|- [scenario...]
 *
 * a32gfx.elf is the driver as tools/le2elf.py makes it, mapped and
 * called as AIL/32 called it (dll.c). First every datum of awedata.c
 * must be the image's bytes at the address it names. Then each
 * scenario, all of them or those named, runs twice, in two processes
 * forked from this one: on the DLL, then on the C, each driving an
 * EMU8000 of its own (emu8k.c) at 220h, with the sound ROM unless it is
 * "-". Each streams every access to the chip's ports (port, width,
 * direction, value, the samples made by then) and, at the end of each
 * step (a procedure called, an interval served), its result where
 * AIL/32's API gives one, the CRC of the driver's writable data (on the
 * DLL the bytes of the library's regions, of the sequencer's and of
 * each registered sequence's state table, and on the C the structures
 * of awedata.c, xmidistate's and xmidiseqstate's bytes, laid out alike)
 * and the samples made since with their CRC. This compares the two
 * streams and reports the first thing that differs. With -c, the blocks
 * of blocks.tsv, each function's basic blocks as Ghidra lists them, each
 * get an INT3 in the DLL's runs, and those left at 0 runs are listed.
 * With -l, the C's accesses to the chip are written to log, a line
 * each: the samples made by then, the port, i or o, the value (in hex),
 * which tools/emu8kdiff replays on
 * 86Box's EMU8000. With one scenario named, the log is that one's.
 *
 * The scenarios, each from the driver's initialisation as lib_midi's
 * InitMidi makes it (described, the card detected at 220h, the driver
 * set up):
 *
 *	tunes	every tune of midi_mi.hqr, then of midi_sb.hqr, as lib_midi
 *		plays one (fmoracle.c): registered, its timbres asked for,
 *		started at volume 100, served 120 times a second for a minute
 *		(or the seconds given), restarted when done, its volume 70 at
 *		20 s, faded to 0 over 2 s at 40 s, 100 at 45 s.
 *	statuses, keys, velocities, controllers, rpn, bends, sustain,
 *	stealing, random
 *		messages through send_cv_msg, the chip run some samples on
 *		between them. statuses: every status byte. keys: every
 *		program on every key, the drum kit on every key byte,
 *		programs past 127. velocities: every velocity byte.
 *		controllers: every controller byte at its edges, over notes
 *		and silence. rpn: RPNs 0 to 2 and NRPNs with their data.
 *		bends: bends at their edges over ranges of 0 to 24
 *		semitones. sustain: the pedal. stealing: 31 to 64 notes at
 *		once, released after varied times so that the voice
 *		allocator meets each of its cases. random: a hundred
 *		thousand messages drawn at random.
 *	overread
 *		volumes past 127, whose attenuation the library reads past
 *		its table, from its own code (run with -c's traps off).
 *	presets	a preset beyond the CD's (a tuning modifier, velocity
 *		zones, a jump back, a negative hold, a high reverb send) over
 *		program 0 and the drum kit, played. Then a second over
 *		program 1 and the kit, of five instruments, its zones by key
 *		(key scaling, holds and decays at the edges of what they are
 *		divided by, an attack of 0x7c, a lower tune, a root past
 *		0x7fff, a pan of 0 under channel 10's pan of 62, sends that
 *		sum to 256, two layers of a short attack, bytes after a zone's
 *		end, keys limited by an instrument), and a third over program
 *		2 whose directory's size ends inside its zone.
 *	kits	channel 10 given each kit from 0x81 to 0x87, as only the
 *		oracle can, its record (past the directory, in the presets' first
 *		bytes) made a program's, and every key byte
 *		played: those the kit plays itself sound that program, the
 *		others the standard kit.
 *	lifecycle
 *		the card looked for at every base. Each of the detection's
 *		five reads made to fail its test, then read as 0, 0xffff,
 *		each bit alone and the chip's value with each bit turned. The
 *		driver set up, every channel's controllers moved, shut down,
 *		set up and shut down again, then looked for at 240h and set
 *		up at 220h.
 *	dead	the dead functions, called: outpw and inpw (at
 *		odd ports too), the port table set again, the equalizer at
 *		each level, the FM voices' setup (at twelve phases of the bit
 *		it waits on), every voice released, a note off, the clamps
 *		and the table search, at their edges.
 *	sequencer
 *		XMIDI sequences of every event and every procedure of AIL/32's
 *		API (suite.c).
 *	sequences
 *		eight sequences at once, states used again, a sequence stopped
 *		and resumed holding pedals, locks and protections, the API's
 *		setters at their edges, files of chunks named near XMIDI's or
 *		past 16 MB, a second initialisation (suite.c).
 *	corners	the sequencer where its tests turn: handles of -1 and let
 *		go, a sequence alone in slot 4, channels mapped odd to even,
 *		percents and ramps past 32 bits, the held controllers at 63,
 *		64, 65 and 0x80 stopped and resumed, bytes of 0xfe and 0xff
 *		released from locked channels, states let go and used again,
 *		counts of notes wrapped below 0, the driver set up again from
 *		a callback, shut down with slots free and taken (suite.c).
 *	queue	more notes than the sequencer's queue holds (suite.c).
 *	listen	with -w alone: every tune of midi_mi.hqr played once to its
 *		end, written as the C plays it into dir, a WAV file each.
 */

enum
{
	Base = 0x220,
	Pitdivisor = 9943,
	Pithz = 1193182,
	Awehz = 44100,
	Romwords = 0x80000,
	Statemax = 4096,
};

typedef struct Datum Datum;

struct Datum
{
	char	*name;
	uint32_t	addr;
	void	*c;
	uint32_t	size;
};

static Datum data[] = {
	{"awenames", 0x1018b, awenames, sizeof awenames},
	{"awemixaddr", 0x101a9, &awemixaddr, 4},
	{"awemixdata", 0x101ad, &awemixdata, 4},
	{"awedataout", 0x101b1, &awedataout, 4},
	{"aweaddrstat", 0x101b5, &aweaddrstat, 4},
	{"aweports", 0x11bb6, aweports, sizeof aweports},
	{"aweinits", 0x11bc6, aweinits, sizeof aweinits},
	{"awestate", 0x11fc6, &awestate, sizeof awestate},
	{"aweeqlevels", 0x124c4, aweeqlevels, sizeof aweeqlevels},
	{"aweeq", 0x124c6, aweeq, sizeof aweeq},
	{"aweunref", 0x131ee, aweunref, sizeof aweunref},
	{"awesearchtab", 0x13272, awesearchtab, sizeof awesearchtab},
	{"awedecaytab", 0x13372, awedecaytab, sizeof awedecaytab},
	{"aweparse", 0x13472, &aweparse, sizeof aweparse},
	{"awedeflayer", 0x136c2, &awedeflayer, sizeof awedeflayer},
	{"awedeflayerpad", 0x13736, awedeflayerpad, sizeof awedeflayerpad},
	{"awetagsize", 0x1373c, awetagsize, sizeof awetagsize},
	{"awemods", 0x13776, awemods, sizeof awemods},
	{"aweatten", 0x13866, aweatten, sizeof aweatten},
	{"awedir", 0x14d2a, awedir, sizeof awedir},
	{"awepresets", 0x15132, awepresets, sizeof awepresets},
};

/* the library's writable regions, as the DLL holds them, the sequencer's last */
static uint32_t regions[][2] = {
	{0x10163, 0x28}, {0x1018b, 0x1e}, {0x101a9, 0x10}, {0x11bb6, 0x10},
	{0x11bc6, 0x400}, {0x11fc6, 0x4fe}, {0x124c4, 2}, {0x13472, 0x250},
	{0x13776, 0xf0}, {0x10358, 0x22b},
};

static uint16_t *rom;
static uint8_t **mi, **sb;
static int nmi, nsb;
static uint8_t *state;
static uint32_t nticks = 120 * 60;

static char *wavdir;
static FILE *wav;
static uint32_t wavframes;
static char *logname;
static FILE *logf;

/* a run's */
static int fault;		/* the detection's read made to fail, from 1 */
static int faultmode;
static int nread;
static uint64_t before;
static uint64_t ticks;

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "aweoracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

/* Every datum of awedata.c the image's bytes, the descriptor's but its pointer to the names. */
static void
checkdata(void)
{
	uint8_t a[sizeof(Drvrdesc)], b[sizeof(Drvrdesc)];
	uint8_t *p;
	size_t i, k;
	int bad;

	if (sizeof(Drvrdesc) != 0x28 || sizeof(Awestate) != 0x4fe || sizeof(Aweparse) != 0x250
	|| sizeof awemods != 0xf0 || sizeof(Awedir) != 8 || sizeof(Layer) != 0x74)
		fatal("the structures are not laid out as the DLL's");
	bad = 0;
	memcpy(a, (void*)(uintptr_t)0x10163, sizeof a);
	memcpy(b, &aweddt, sizeof b);
	if (get32(a + 12) != 0x1018b || aweddt.names != awenames)
		bad++, printf("aweoracle: aweddt: the names are not awenames\n");
	memset(a + 12, 0, 4);
	memset(b + 12, 0, 4);
	if (memcmp(a, b, sizeof a) != 0)
		bad++, printf("aweoracle: aweddt differs from the image at 0x10163\n");
	for (i = 0; i < nelem(data); i++) {
		p = (uint8_t*)(uintptr_t)data[i].addr;
		if (memcmp(p, data[i].c, data[i].size) == 0)
			continue;
		for (k = 0; p[k] == ((uint8_t*)data[i].c)[k]; k++)
			;
		printf("aweoracle: %s differs from the image at %#x: %02x, ours %02x\n",
			data[i].name, data[i].addr + (uint32_t)k, p[k], ((uint8_t*)data[i].c)[k]);
		bad++;
	}
	if (bad)
		exit(1);
	printf("aweoracle: %d data, the image's bytes\n", (int)nelem(data) + 1);
}

/*
 * statecrc: the CRC of the driver's writable data, the DLL's regions or
 * awedata.c's structures and the sequencer's variables (xmidistate),
 * then each registered sequence's state table.
 */
uint32_t
statecrc(void)
{
	uint8_t d[sizeof(Drvrdesc)], q[0x22b];
	uint32_t c;
	size_t i;

	c = 0;
	if (indll) {
		memcpy(d, (void*)(uintptr_t)regions[0][0], sizeof d);
		memset(d + 12, 0, 4);
		c = crc(c, d, sizeof d);
		for (i = 1; i < nelem(regions); i++)
			c = crc(c, (uint8_t*)(uintptr_t)regions[i][0], regions[i][1]);
		return seqcrc(c, (uint8_t*)(uintptr_t)0x10358);
	}
	memcpy(d, &aweddt, sizeof d);
	memset(d + 12, 0, 4);
	c = crc(c, d, sizeof d);
	c = crc(c, (uint8_t*)awenames, sizeof awenames);
	c = crc(c, (uint8_t*)&awemixaddr, 4);
	c = crc(c, (uint8_t*)&awemixdata, 4);
	c = crc(c, (uint8_t*)&awedataout, 4);
	c = crc(c, (uint8_t*)&aweaddrstat, 4);
	c = crc(c, (uint8_t*)aweports, sizeof aweports);
	c = crc(c, (uint8_t*)aweinits, sizeof aweinits);
	c = crc(c, (uint8_t*)&awestate, sizeof awestate);
	c = crc(c, aweeqlevels, sizeof aweeqlevels);
	c = crc(c, (uint8_t*)&aweparse, sizeof aweparse);
	c = crc(c, (uint8_t*)awemods, sizeof awemods);
	xmidistate(q);
	return seqcrc(crc(c, q, sizeof q), q);
}

/*
 * The value a read of the detection gives in place of val: with mode
 * -1 the bits its test looks at, the other way, then 0, 0xffff, each
 * bit alone, and val with each bit turned.
 */
static uint32_t
faulty(uint32_t val)
{
	if (faultmode < 0)
		switch (fault) {
		case 1: return val & ~0x0cu;
		case 2: return val & ~0x58u;
		case 3: return val & ~0x01u;
		case 4: return val & ~0x02u;
		default: return val | 0x40;
		}
	if (faultmode == 0)
		return 0;
	if (faultmode == 1)
		return 0xffff;
	if (faultmode < 18)
		return 1u << (faultmode - 2);
	return val ^ 1u << (faultmode - 18);
}

/* An access to the card's ports, made on the chip and streamed. */
static uint32_t
chipio(uint32_t port, uint32_t val, int w, int in)
{
	if (in) {
		val = emu8kin((port - Base) & 0xffff);
		if (fault != 0 && ++nread == fault)
			val = faulty(val);
	} else
		emu8kout((port - Base) & 0xffff, val);
	emitio((port & 0xffff) | w<<16 | in<<24, val, emu8kmade());
	if (logf != NULL && !indll)
		fprintf(logf, "%llu %x %c %x\n", (unsigned long long)emu8kmade(), port & 0xffff, in ? 'i' : 'o', val);
	return val;
}

static void
dllout(uint32_t port, uint32_t val, int w)
{
	chipio(port, val, w, 0);
}

static uint32_t
dllin(uint32_t port, int w)
{
	return chipio(port, 0, w, 1);
}

/* The decompilation's I/O. */
void
aweout(uint32_t port, uint32_t val)
{
	chipio(port, val & 0xffff, 2, 0);
}

uint32_t
awein(uint32_t port)
{
	return chipio(port, 0, 2, 1);
}

/*
 * The chip's samples: counted and summed, and written when a tune is
 * listened to, at the music's volume SETUP gives the AWE32 (195, -14 dB,
 * 13076 in Q16) and cut to 16 bits, as the port plays them.
 */
static void
sink(int32_t *o, int n)
{
	int16_t s[2];
	int64_t x;
	int i, k;

	rcrc = crc(rcrc, (uint8_t*)o, 8*n);
	made += n;
	if (wav == NULL)
		return;
	for (i = 0; i < n; i++) {
		for (k = 0; k < 2; k++) {
			x = (int64_t)o[2*i+k] * 13076;
			x = x >= 0 ? x >> 16 : -((-x - 1) >> 16) - 1;
			s[k] = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
		}
		if (fwrite(s, 2, 2, wav) != 2)
			fatal("cannot write a tune");
		wavframes++;
	}
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* A WAV file's header: 16-bit stereo at 44100 Hz, n frames. */
static void
wavheader(uint8_t *h, uint32_t n)
{
	memcpy(h, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
	put32(h+4, 36 + 4*n);
	put32(h+24, Awehz);
	put32(h+28, 4*Awehz);
	put32(h+40, 4*n);
}

static void
wavclose(void)
{
	uint8_t h[44];

	if (wav == NULL)
		return;
	wavheader(h, wavframes);
	if (fseek(wav, 0, SEEK_SET) < 0 || fwrite(h, 1, 44, wav) != 44 || fclose(wav) != 0)
		fatal("cannot write a tune");
	wav = NULL;
}

/* The C's samples of a tune, from now, into wavdir/name.wav. */
static void
wavopen(char *name)
{
	char path[1024];
	uint8_t h[44];

	wavclose();
	if (indll || wavdir == NULL)
		return;
	snprintf(path, sizeof path, "%s/%s.wav", wavdir, name);
	wav = fopen(path, "wb");
	wavheader(h, 0);
	if (wav == NULL || fwrite(h, 1, 44, wav) != 44)
		fatal("cannot create %s", path);
	wavframes = 0;
}

/* interval: the chip's samples to the start of the clock's next interval, as midi.c makes them, then the service. */
void
interval(void)
{
	uint64_t n, t;

	n = ticks + 1;
	t = n/Pithz * Pitdivisor*1000000000ull + n%Pithz * Pitdivisor*1000000000ull / Pithz;
	emu8krun(before + t * Awehz / 1000000000);
	ticks++;
	api(103, 0);
}

/* initmidi: lib_midi's InitMidi, the driver described, the card looked for, the driver set up. */
void
initmidi(void)
{
	uint32_t r, n;

	r = api(100, 0);
	step("describe", indll ? r == 0x10163 : r == (uint32_t)(uintptr_t)&aweddt);
	step("detect", api(101, 4, Base, -1, -1, -1));
	api(102, 4, Base, -1, -1, -1);
	step("init", 0);
	api(150, 0);
	step("statesize", 0);
	n = api(153, 0);
	step("cachesize", n);
	if (n != 0)
		fatal("the driver wants a timbre cache");
	before = made;
	ticks = 0;
}

/* One bank's tunes as lib_midi's PlayMidi and the game play them. */
static void
playbank(char *name, uint8_t **tunes, int ntunes)
{
	uint32_t tick, treq;
	int32_t h;
	int t;

	h = -1;
	for (t = 0; t < ntunes; t++) {
		if (tunes[t] == NULL)
			continue;
		if (h != -1) {
			api(171, 1, h);
			api(152, 1, h);
			step("%s %d: stop, release", 0, name, t);
		}
		h = api(151, 4, (uint32_t)(uintptr_t)tunes[t], 0, (uint32_t)(uintptr_t)state, 0);
		step("%s %d: register", h, name, t);
		if (h == -1)
			continue;
		treq = api(155, 1, h);
		step("%s %d: timbre request", treq, name, t);
		if ((treq & 0xffff) != 0xffff)
			fatal("the driver asks for a timbre");
		api(170, 1, h);
		api(177, 3, 0, 100, 0);
		step("%s %d: start, volume 100", 0, name, t);
		for (tick = 0; tick < nticks; tick++) {
			interval();
			if (api(174, 1, 0) == 2)
				api(170, 1, h);
			switch (tick) {
			case 20*120:
				api(177, 3, 0, 70, 0);
				break;
			case 40*120:
				api(177, 3, 0, 0, 2000);
				break;
			case 45*120:
				api(177, 3, 0, 100, 0);
				break;
			}
			step("%s %d: interval %u", 0, name, t, tick);
		}
	}
	if (h != -1) {
		api(171, 1, h);
		api(152, 1, h);
		step("%s: stop, release", 0, name);
	}
}

static void
tunes(void)
{
	initmidi();
	playbank("MIDI_MI", mi, nmi);
	playbank("MIDI_SB", sb, nsb);
}

/* Every tune of midi_mi.hqr played once to its end, three minutes at most, and two seconds more. */
static void
listentunes(void)
{
	char name[16];
	uint32_t tick;
	int32_t h;
	int t;

	initmidi();
	for (t = 0; t < nmi; t++) {
		if (mi[t] == NULL)
			continue;
		h = api(151, 4, (uint32_t)(uintptr_t)mi[t], 0, (uint32_t)(uintptr_t)state, 0);
		step("MIDI_MI %d: register", h, t);
		if (h == -1)
			continue;
		snprintf(name, sizeof name, "mi%02d", t);
		wavopen(name);
		api(170, 1, h);
		api(177, 3, 0, 100, 0);
		for (tick = 0; tick < 120*180 && api(174, 1, h) != 2; tick++) {
			interval();
			step("MIDI_MI %d: interval %u", 0, t, tick);
		}
		for (tick = 0; tick < 240; tick++)
			interval();
		api(171, 1, h);
		api(152, 1, h);
		step("MIDI_MI %d: done", 0, t);
		wavclose();
	}
}

/* A message through send_cv_msg, a step. */
static void
msg(uint32_t st, uint32_t d1, uint32_t d2)
{
	step("cv %02x %02x %02x", api(186, 3, st, d1, d2), st, d1, d2);
}

/* The chip run n samples on, a step. */
static void
chiprun(uint32_t n)
{
	emu8krun(made + n);
	step("run %u", 0, n);
}

/* Every channel's notes off, and their releases over. */
static void
silence(void)
{
	uint32_t ch;

	for (ch = 0; ch < 16; ch++)
		msg(0xb0|ch, 123, 0);
	chiprun(8192);
}

static void
statuses(void)
{
	uint32_t s;

	initmidi();
	for (s = 0; s < 0x100; s++) {
		msg(s, 0x3c, 0x64);
		chiprun(200);
	}
	silence();
}

static void
keys(void)
{
	uint32_t p, k;

	initmidi();
	for (p = 0; p < 0x100; p++) {
		msg(0xc0, p, 0);
		for (k = 0; k < (p < 0x80 ? 0x80u : 1u); k++) {
			msg(0x90, k, 0x64);
			chiprun(64);
			msg(0x80, k, 0x40);
		}
	}
	msg(0xc0, 0, 0);
	for (k = 0x80; k < 0x100; k++) {
		msg(0x90, k, 0x64);
		msg(0x91, k, 0x64);
		chiprun(64);
		msg(0x80, k, 0);
		msg(0x81, k, 0);
	}
	for (k = 0; k < 0x100; k++) {
		msg(0x99, k, 0x64);
		chiprun(64);
		msg(0x89, k, 0);
	}
	msg(0xc9, 5, 0);
	msg(0x99, 0x24, 0x64);
	silence();
}

static void
velocities(void)
{
	uint32_t v, ch;

	initmidi();
	msg(0xc0, 0x13, 0);
	for (ch = 0; ch < 16; ch += 9)
		for (v = 0; v < 0x100; v++) {
			msg(0x90|ch, ch == 9 ? 0x26 : 0x3c, v);
			chiprun(32);
			msg(0x80|ch, ch == 9 ? 0x26 : 0x3c, 0);
		}
	silence();
}

static uint32_t edges[] = {0, 1, 0x3f, 0x40, 0x41, 0x7e, 0x7f, 0x80, 0xc0, 0xff};

static void
controllers(void)
{
	uint32_t c, i, ch;

	initmidi();
	for (ch = 0; ch < 16; ch += 9) {
		msg(0xc0|ch, 0x30, 0);
		for (c = 0; c < 0x100; c++)
			for (i = 0; i < nelem(edges); i++) {
				/* volumes past 127: overread */
				if (c == 7 && edges[i] >= 0x80)
					continue;
				msg(0x90|ch, 0x40, 0x64);
				msg(0x90|ch, 0x43, 0x50);
				msg(0xb0|ch, c, edges[i]);
				chiprun(48);
				msg(0x80|ch, 0x40, 0);
				msg(0xb0|ch, c, edges[i]);
				msg(0x80|ch, 0x43, 0);
				chiprun(16);
			}
		msg(0xb0|ch, 121, 0);
	}
	silence();
}

/* Parameter number msb, lsb, as an RPN or an NRPN. */
static void
param(uint32_t nrpn, uint32_t msb, uint32_t lsb)
{
	msg(0xb0, nrpn ? 99 : 101, msb);
	msg(0xb0, nrpn ? 98 : 100, lsb);
}

static void
rpn(void)
{
	static uint32_t nums[][2] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 0}, {2, 0}, {0, 0x7f}, {0x7f, 0x7f}, {0x7f, 0}};
	uint32_t i, k, n;

	initmidi();
	param(1, 1, 2);
	msg(0xb0, 6, 5);
	msg(0xb0, 38, 3);
	msg(0xb0, 121, 0);
	msg(0xb0, 6, 5);
	msg(0xb0, 99, 0x7f);
	msg(0xb0, 98, 0x7f);
	for (n = 0; n < 2; n++)
		for (i = 0; i < nelem(nums); i++) {
			param(n, nums[i][0], nums[i][1]);
			for (k = 0; k < nelem(edges); k++) {
				msg(0xb0, 6, edges[k]);
				msg(0xb0, 38, edges[nelem(edges)-1-k]);
				msg(0x90, 0x3c, 0x64);
				msg(0xe0, 0x7f, 0x7f);
				chiprun(32);
				msg(0x80, 0x3c, 0);
				msg(0xb0, 38, edges[k]);
			}
		}
	msg(0xb0, 121, 0);
	param(0, 0, 0);
	msg(0xb0, 6, 0x0c);
	msg(0x90, 0x3c, 0x64);
	msg(0xe0, 0, 0);
	silence();
}

static void
bends(void)
{
	uint32_t r, a, b;

	initmidi();
	msg(0xc0, 0x28, 0);
	for (r = 0; r <= 24; r++) {
		param(0, 0, 0);
		msg(0xb0, 6, r);
		msg(0x90, 0x3c, 0x64);
		msg(0x90, 0x48, 0x64);
		for (a = 0; a < nelem(edges); a++)
			for (b = 0; b < nelem(edges); b++) {
				msg(0xe0, edges[a], edges[b]);
				chiprun(8);
			}
		msg(0x80, 0x3c, 0);
		msg(0x90, 0x3e, 0x64);
		chiprun(32);
		msg(0x80, 0x3e, 0);
		msg(0x80, 0x48, 0);
	}
	msg(0xe0, 0, 0x40);
	silence();
}

static void
sustain(void)
{
	uint32_t i;

	initmidi();
	for (i = 0; i < nelem(edges); i++) {
		msg(0x90, 0x3c, 0x64);
		msg(0x90, 0x40, 0x64);
		msg(0xb0, 64, edges[i]);
		msg(0x80, 0x3c, 0);
		chiprun(256);
		msg(0x90, 0x3c, 0x64);
		msg(0x80, 0x3c, 0);
		msg(0xb0, 64, edges[nelem(edges)-1-i]);
		chiprun(256);
		msg(0x80, 0x40, 0);
		msg(0xb0, 64, 0);
		chiprun(256);
	}
	msg(0xb0, 64, 0x7f);
	msg(0xb0, 64, 0);
	silence();
}

static void
stealing(void)
{
	static uint32_t progs[] = {0, 16, 19, 48, 52, 88, 91};
	uint32_t n, i, ch, w;

	initmidi();
	seed = 7;
	for (ch = 0; ch < 8; ch++)
		msg(0xc0|ch, progs[ch % nelem(progs)], 0);
	for (n = 31; n <= 64; n++) {
		for (i = 0; i < n; i++) {
			msg(0x90|(i % 8), 0x24 + (i*7 + n) % 0x48, 0x40 + rnd(0x40));
			chiprun(rnd(64));
		}
		w = rnd(4) == 0 ? 0 : 1u << rnd(16);
		chiprun(w);
		for (i = 0; i < n; i += 2)
			msg(0x80|(i % 8), 0x24 + (i*7 + n) % 0x48, 0);
		chiprun(rnd(1u << rnd(16)));
		for (i = 0; i < 8; i++) {
			msg(0x90|i, 0x30 + i, 0x7f);
			chiprun(rnd(256));
		}
		if (n % 4 == 0)
			silence();
	}
	silence();
}

static void
randoms(void)
{
	static uint32_t ctrls[] = {1, 6, 7, 10, 38, 64, 91, 93, 98, 99, 100, 101, 121, 123};
	uint32_t i, ch, c, v;

	initmidi();
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
			if (c == 7)
				v &= 0x7f;
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
			chiprun(rnd(2048));
	}
	silence();
}

static void
overread(void)
{
	uint32_t v;

	initmidi();
	msg(0xc0, 0x30, 0);
	for (v = 0x80; v < 0x100; v++) {
		msg(0x90, 0x3c, 0x64);
		msg(0xb0, 7, v);
		msg(0x90, 0x40, 0x64);
		chiprun(32);
		msg(0x80, 0x3c, 0);
		msg(0x80, 0x40, 0);
	}
	msg(0xb0, 7, 100);
	silence();
}

/* A byte of the directory or the presets, on either side. */
static void
poke(uint32_t a, uint8_t *c, uint32_t v)
{
	if (indll)
		*(uint8_t*)(uintptr_t)a = v;
	else
		*c = v;
}

/* Channel 10's preset set directly: kits 0x81 to 0x87 lie beyond every message. */
static void
setkit(uint32_t kit)
{
	if (indll) {
		*(uint8_t*)(uintptr_t)0x123ea = kit;
		*(uint8_t*)(uintptr_t)0x123eb = kit >> 8;
	} else
		awestate.chans[9].preset = kit;
	step("kit %02x", 0, kit);
}

/*
 * A kit's record, past the directory in the presets' first bytes, made
 * program p's, on either side: the DLL holds those bytes once, the C
 * twice (awedir's last records, awepresets' first bytes).
 */
static void
kitrecord(uint32_t kit, uint32_t p)
{
	uint32_t i, a;
	uint8_t *r;

	r = (uint8_t*)&awedir[p];
	a = 8 * (kit - 0x81);
	for (i = 0; i < 8; i++) {
		poke(0x15132 + a + i, (uint8_t*)&awedir[kit] + i, r[i]);
		if (!indll)
			awepresets[a + i] = r[i];
	}
	step("kit %02x record", 0, kit);
}

static void
kits(void)
{
	uint32_t kit, k;

	initmidi();
	for (kit = 0x81; kit <= 0x87; kit++) {
		setkit(kit);
		kitrecord(kit, kit - 0x80);
		for (k = 0; k < 0x100; k++) {
			msg(0x99, k, 0x64);
			chiprun(32);
			msg(0x89, k, 0);
		}
	}
	setkit(0x80);
	silence();
}

/*
 * A preset beyond the CD's, as Creative's library reads them: a
 * modifier of the tuning, zones by velocity, a jump back to the
 * instrument's end, a negative hold, a reverb send of 200.
 */
static uint8_t synthpreset[] = {
	Tinstr, 76, 0, Tatten, 100,
	Tzone, 10, Ttune, 50, 0, Tvolhold, 0x00, 0x90, Treverb, 200,
	Tzone, 27, Tkeys, 0x00, 0x7f, Tvels, 0x00, 0x40, Troot, 0x70, 0x17, Tmodes, 1,
	Tend, 0, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00,
	Tzone, 28, Tkeys, 0x00, 0x7f, Tvels, 0x41, 0x7f, Ttune, 0xe2, 0xff, Troot, 0x70, 0x17,
	Tend, 1, Tstart, 0x6b, 0x45, 0x00, Tloopstart, 0x4f, 0x8a, 0x00, Tloopend, 0xd8, 0x9b, 0x00,
	Tback,
	Tzone, 5, Tatten, 0x10, 0,
};

/*
 * A second, of five instruments. The first's zones each on keys of
 * their own: key scaling, holds of 91 and 92, decays from 0x8000 up, an
 * attack of 0x7c, a tune made lower, a root past 0x7fff, a pan of 0, two
 * layers of a short attack, one setting its velocity, bytes after a
 * zone's end, its reverb and chorus sends 200. Then one limited by
 * keys, one left by a jump back with its modifiers still counted, one
 * limited by keys, and one of a zone, which takes the modifiers left.
 */
static uint8_t synthpreset2[] = {
	Tinstr, 250, 0, Treverb, 0xc8, Tchorus, 0xc8, Tzone, 26, Tkeys, 0x00, 0x0f, Tkeyhold, 0x05,
	Tkeydecay, 0x07, Tvolhold, 0x5b, 0x00, Tend, 0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c,
	0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tzone, 22, Tkeys, 0x10, 0x1f, Tvoldecay, 0x00, 0xc0, Tend,
	0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tzone,
	25, Tkeys, 0x20, 0x2f, Tvoldecay, 0x00, 0x90, Tvolhold, 0x5c, 0x00, Tend, 0x00, Tstart, 0xee,
	0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tzone, 21, Tkeys, 0x30,
	0x3f, Tvolattack, 0x7c, Tend, 0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00,
	Tloopend, 0x37, 0x45, 0x00, Tzone, 22, Tkeys, 0x40, 0x4f, Ttune, 0xe2, 0xff, Tend, 0x00, Tstart,
	0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tzone, 22, Tkeys,
	0x50, 0x5f, Troot, 0x00, 0x80, Tend, 0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00,
	Tloopend, 0x37, 0x45, 0x00, Tzone, 21, Tkeys, 0x60, 0x6f, Tpan, 0x00, Tend, 0x00, Tstart, 0xee,
	0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tzone, 23, Tkeys, 0x70,
	0x77, Tvolattack, 0x20, Tvel, 0x64, Tend, 0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b,
	0x00, Tloopend, 0x37, 0x45, 0x00, Tzone, 21, Tkeys, 0x70, 0x77, Tvolattack, 0x20, Tend, 0x00,
	Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tzone, 21,
	Tkeys, 0x78, 0x7f, Tend, 0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend,
	0x37, 0x45, 0x00, Tatten, 0x32, Tzone, 19, Tkeys, 0x78, 0x7f, Tend, 0x00, Tstart, 0xee, 0x01,
	0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tinstr, 24, 0, Tkeys, 0x00, 0x3f,
	Tatten, 0x1e, Tzone, 16, Tend, 0x00, Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00,
	Tloopend, 0x37, 0x45, 0x00, Tinstr, 25, 0, Tatten, 0x28, Tzone, 19, Tkeys, 0x00, 0x7f, Tend, 0x00,
	Tstart, 0xee, 0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00, Tback, Tinstr,
	6, 0, Tkeys, 0x00, 0x3f, Tinstr, 22, 0, Tzone, 19, Tkeys, 0x40, 0x7f, Tend, 0x00, Tstart, 0xee,
	0x01, 0x00, Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00,
};

/* A third, whose directory's size ends after its first byte of Tend. */
static uint8_t synthpreset3[] = {
	Tinstr, 24, 0, Tzone, 21, Tkeys, 0x00, 0x7f, Tatten, 0x14, Tend, 0x00, Tstart, 0xee, 0x01, 0x00,
	Tloopstart, 0x8c, 0x2b, 0x00, Tloopend, 0x37, 0x45, 0x00,
};

/* The synthetic preset over program 0's, and the drum kit's entry made its. */
static void
inject(void)
{
	uint32_t off, i;

	off = awedir[0].off;
	for (i = 0; i < sizeof synthpreset; i++)
		poke(0x15132 + off + i, &awepresets[off + i], synthpreset[i]);
	poke(0x14d2a, (uint8_t*)&awedir[0].size, sizeof synthpreset);
	poke(0x14d2b, (uint8_t*)&awedir[0].size + 1, 0);
	poke(0x14d2a + 8*0x80, (uint8_t*)&awedir[0x80].size, sizeof synthpreset);
	poke(0x14d2b + 8*0x80, (uint8_t*)&awedir[0x80].size + 1, 0);
	poke(0x14d2c + 8*0x80, (uint8_t*)&awedir[0x80].off, off & 0xff);
	poke(0x14d2d + 8*0x80, (uint8_t*)&awedir[0x80].off + 1, off >> 8);
	step("inject", 0);
}

/* Preset bytes at off, and directory record r made theirs, size bytes long, on either side. */
static void
putpreset(uint32_t r, uint32_t off, uint8_t *b, uint32_t n, uint32_t size)
{
	uint8_t rec[4];
	uint32_t i;

	for (i = 0; i < n; i++)
		poke(0x15132 + off + i, &awepresets[off + i], b[i]);
	rec[0] = size;
	rec[1] = size >> 8;
	rec[2] = off;
	rec[3] = off >> 8;
	for (i = 0; i < 4; i++)
		poke(0x14d2a + 8*r + i, (uint8_t*)&awedir[r] + i, rec[i]);
}

static void
presets(void)
{
	static uint32_t vels[] = {1, 40, 64, 65, 100, 127, 200};
	static uint32_t keys[] = {0x24, 0x3c, 0x60};
	uint32_t ch, v, k;

	initmidi();
	inject();
	for (ch = 0; ch < 16; ch += 9) {
		msg(0xb0|ch, 91, 0x7f);
		msg(0xb0|ch, 93, 0x7f);
		for (v = 0; v < nelem(vels); v++)
			for (k = 0; k < nelem(keys); k++) {
				msg(0x90|ch, keys[k], vels[v]);
				chiprun(64);
				msg(0x80|ch, keys[k], 0);
			}
	}
	silence();
	putpreset(1, 0xce, synthpreset2, sizeof synthpreset2, sizeof synthpreset2);
	putpreset(0x80, 0xce, synthpreset2, 0, sizeof synthpreset2);
	putpreset(2, 0x220, synthpreset3, sizeof synthpreset3, 11);
	step("inject 2 and 3", 0);
	msg(0xc1, 1, 0);
	msg(0xc2, 2, 0);
	for (ch = 1; ch < 16; ch += 8) {
		msg(0xb0|ch, 91, 94);
		msg(0xb0|ch, 93, 94);
	}
	msg(0xb9, 10, 62);
	for (k = 0; k < 0x80; k += 4)
		for (v = 0; v < 2; v++)
			for (ch = 1; ch < 16; ch += 8) {
				msg(0x90|ch, k + 1, v ? 100 : 10);
				chiprun(64);
				msg(0x80|ch, k + 1, 0);
			}
	msg(0x92, 0x3c, 100);
	chiprun(64);
	msg(0x82, 0x3c, 0);
	silence();
}

static void
lifecycle(void)
{
	uint32_t b, ch;
	int f, m;

	step("describe", api(100, 0) != 0);
	for (b = 0x200; b <= 0x2a0; b += 0x20)
		step("detect at %x", api(101, 4, b, -1, -1, -1), b);
	for (f = 1; f <= 5; f++)
		for (m = -1; m < 34; m++) {
			fault = f;
			faultmode = m;
			nread = 0;
			step("detect, read %d as %d", api(101, 4, Base, -1, -1, -1), f, m);
			fault = 0;
		}
	for (f = 0; f < 2; f++) {
		step("detect", api(101, 4, Base, -1, -1, -1));
		api(102, 4, Base, -1, -1, -1);
		step("init", 0);
		msg(0x90, 0x3c, 0x64);
		msg(0x91, 0x40, 0x64);
		msg(0x99, 0x26, 0x64);
		/* every channel's controllers away from their start, for the next init */
		for (ch = 0; ch < 16; ch++) {
			msg(0xc0|ch, 5 + ch, 0);
			msg(0xb0|ch, 1, 0x40);
			msg(0xb0|ch, 7, 0x50);
			msg(0xb0|ch, 10, 0x20);
			msg(0xb0|ch, 64, 0x7f);
			msg(0xb0|ch, 91, 0x30);
			msg(0xb0|ch, 93, 0x30);
			msg(0xb0|ch, 101, 0);
			msg(0xb0|ch, 100, 0);
			msg(0xb0|ch, 6, 7);
			msg(0xb0|ch, 38, 3);
			msg(0xe0|ch, 0x10, 0x50);
		}
		chiprun(1024);
		api(104, 1, (uint32_t)(uintptr_t)"");
		step("shutdown", 0);
		chiprun(8192);
	}
	/* looked for elsewhere, then set up at the card's base: init_driver gives the ports */
	step("detect at 240", api(101, 4, 0x240, -1, -1, -1));
	api(102, 4, Base, -1, -1, -1);
	step("init", 0);
	msg(0x90, 0x3c, 0x64);
	chiprun(1024);
	api(104, 1, (uint32_t)(uintptr_t)"");
	step("shutdown", 0);
}

/* A function of the DLL at a, or the C's, called with its arguments (C, or Pascal for one). */
static uint32_t
call(uint32_t a, int pascal, int na, ...)
{
	uint32_t x[4];
	va_list arg;
	int i;

	va_start(arg, na);
	for (i = 0; i < na; i++)
		x[i] = va_arg(arg, uint32_t);
	va_end(arg);
	if (indll)
		return dllcall(a, pascal, na, x);
	switch (a) {
	case 0x1030a: aweoutpw(x[0], x[1]); return 0;
	case 0x10335: return aweinpw(x[0]);
	case 0x11a94: return awe32detect(x[0]);
	case 0x129b6: aweseteq(); return 0;
	case 0x12d5b: aweinitfm(); return 0;
	case 0x12f3a: awereleaseall(); return 0;
	case 0x13a66: return awenoteoff(x[0], x[1]);
	case 0x13ef7: return aweclamp16(x[0], x[1], x[2]);
	case 0x14cc2: return aweclamp127(x[0]);
	case 0x14ce0: return awesearch(x[0]);
	}
	fatal("no function %#x in the decompilation", a);
	return 0;
}

static void
dead(void)
{
	static uint32_t ports[] = {Base, Base+0x400, Base+0x402, Base+0x800, Base+0x802, Base+0xc00, Base+0xc02, 0x388, 0x10620,
		Base+1, Base+0x403, 0x10621};
	static uint32_t bases[] = {0x220, 0x240, 0x260, 0x280, 0xfe20, 0x1fc00};
	static uint32_t xs[] = {0, 1, 0x7e, 0x7f, 0x80, 0x7fff, 0x8000, 0xffff, 0x10000, 0x1007f, 0x8000007f, 0xffffffff};
	uint32_t i, j, k;

	initmidi();
	for (i = 0; i < nelem(ports); i++) {
		call(0x10335, 0, 1, ports[i]);
		call(0x1030a, 0, 2, ports[i], 0x1234 + 0x101*i);
		step("outpw %x", 0, ports[i]);
		step("inpw %x", call(0x10335, 0, 1, ports[i]), ports[i]);
	}
	for (i = 0; i < nelem(bases); i++)
		step("awe32Detect %x", call(0x11a94, 1, 1, bases[i]), bases[i]);
	call(0x11a94, 1, 1, Base);
	step("awe32Detect %x", 0, Base);
	for (i = 0; i < 12; i++)
		for (j = 0; j < 12; j++) {
			if (indll) {
				*(uint8_t*)(uintptr_t)0x124c4 = i;
				*(uint8_t*)(uintptr_t)0x124c5 = j;
			} else {
				aweeqlevels[0] = i;
				aweeqlevels[1] = j;
			}
			call(0x129b6, 0, 0);
			step("seteq %u %u", 0, i, j);
		}
	/* the FM voices' setup at twelve phases of the pointer's bit 12, a read a microsecond */
	for (i = 0; i < 12; i++) {
		for (j = 0; j < i; j++)
			call(0x10335, 0, 1, Base+0x802);
		call(0x12d5b, 0, 0);
		step("initfm %u", 0, i);
	}
	chiprun(1024);
	for (i = 0; i < 4; i++) {
		msg(0x90|i, 0x3c + i, 0x64);
		msg(0x90|i, 0x40 + i, 0x64);
	}
	chiprun(512);
	for (i = 0; i < 4; i++)
		step("noteoff %u", call(0x13a66, 0, 2, i, 0x3c + i), i);
	chiprun(512);
	call(0x12f3a, 0, 0);
	step("releaseall", 0);
	chiprun(4096);
	for (i = 0; i < nelem(xs); i++)
		for (j = 0; j < nelem(xs); j++)
			for (k = 0; k < nelem(xs); k++)
				step("clamp16 %x %x %x", call(0x13ef7, 0, 3, xs[i], xs[j], xs[k]), xs[i], xs[j], xs[k]);
	for (i = 0; i < nelem(xs); i++)
		step("clamp127 %x", call(0x14cc2, 0, 1, xs[i]), xs[i]);
	for (i = 0; i < 13000; i++)
		step("search %u", call(0x14ce0, 0, 1, i), i);
	for (i = 0; i < nelem(xs); i++)
		step("search %x", call(0x14ce0, 0, 1, xs[i]), xs[i]);
	silence();
}

static Scenario scenarios[] = {
	{"tunes", tunes, 1},
	{"statuses", statuses, 1},
	{"keys", keys, 1},
	{"velocities", velocities, 1},
	{"controllers", controllers, 1},
	{"rpn", rpn, 1},
	{"bends", bends, 1},
	{"sustain", sustain, 1},
	{"stealing", stealing, 1},
	{"random", randoms, 1},
	{"overread", overread, 0},
	{"kits", kits, 1},
	{"lifecycle", lifecycle, 1},
	{"dead", dead, 1},
	{"presets", presets, 1},
	{"sequencer", sequencer, 1},
	{"sequences", sequences, 1},
	{"queue", queue, 1},
	{"corners", corners, 1},
	{"listen", listentunes, 1},
};

/* timbreof: an empty buffer for an install, which the AWE32's stubs ignore. */
uint8_t*
timbreof(uint32_t bank, uint32_t num)
{
	static uint8_t t[16];

	USED(bank);
	USED(num);
	return t;
}

/* runinit: a run's chip, and on the DLL its I/O, on the C with -l the log of its accesses. */
void
runinit(void)
{
	emu8kreset(rom, sink);
	if (indll)
		dllio(dllout, dllin);
	else if (logname != NULL) {
		logf = fopen(logname, "w");
		if (logf == NULL)
			fatal("cannot create %s", logname);
	}
}

/* The sound ROM, put back if it is a dump one word late. */
static uint16_t*
loadrom(char *path)
{
	uint8_t *b;
	uint16_t *r;
	long n;
	uint32_t i;

	b = readfile(path, &n);
	if (n != 2*Romwords)
		fatal("%s: not a 1 MB ROM", path);
	r = malloc(2*Romwords);
	if (r == NULL)
		fatal("out of memory");
	for (i = 0; i < Romwords; i++)
		r[i] = b[2*i] | b[2*i+1]<<8;
	free(b);
	if (r[3] == 0x314d && r[4] == 0x474d) {
		memmove(r, r+1, 2*(Romwords-1));
		r[Romwords-1] = 0;
	}
	if (r[2] != 0x314d || r[3] != 0x474d)
		fatal("%s: not an AWE32's ROM", path);
	return r;
}

int
main(int argc, char *argv[])
{
	char *cover;
	int bad, i, k;

	cover = NULL;
	while (argc > 2 && argv[1][0] == '-') {
		if (strcmp(argv[1], "-s") == 0)
			nticks = 120 * strtoul(argv[2], NULL, 10);
		else if (strcmp(argv[1], "-c") == 0)
			cover = argv[2];
		else if (strcmp(argv[1], "-w") == 0)
			wavdir = argv[2];
		else if (strcmp(argv[1], "-l") == 0)
			logname = argv[2];
		else
			break;
		argc -= 2;
		argv += 2;
	}
	if (argc < 5) {
		fprintf(stderr, "usage: aweoracle [-s seconds] [-c blocks.tsv] [-w dir] [-l log] a32gfx.elf midi_mi.hqr midi_sb.hqr awe32.raw|- [scenario...]\n");
		return 2;
	}
	asked("aweoracle", argv+5, argc-5, scenarios, nelem(scenarios));
	crcinit();
	synthc = &awe32;
	for (i = 0; i < Nfiles; i++)
		xmidi(i);
	dllload(argv[1]);
	if (cover != NULL)
		readblocks(cover);
	checkdata();
	nmi = hqr(argv[2], &mi);
	nsb = hqr(argv[3], &sb);
	if (strcmp(argv[4], "-") != 0)
		rom = loadrom(argv[4]);
	state = calloc(1, Statemax);
	if (state == NULL)
		fatal("out of memory");
	if (statesize() > Statemax)
		fatal("a sequence's state is too big");
	bad = 0;
	for (i = 0; i < (int)nelem(scenarios); i++) {
		if (scenarios[i].fn == listentunes && wavdir == NULL)
			continue;
		if (argc > 5) {
			for (k = 5; k < argc; k++)
				if (strcmp(argv[k], scenarios[i].name) == 0)
					break;
			if (k == argc)
				continue;
		}
		bad |= scenario("aweoracle", &scenarios[i]);
	}
	if (cover != NULL)
		coverage("aweoracle");
	return bad;
}
