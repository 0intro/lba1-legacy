#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sys/dat.h"
#include "../sys/fns.h"

/*
 * emu8ktest: the EMU8000 (sys/emu8k.c) against what E-mu's guide and
 * Creative's ADIP say of it, on data it makes: waves written into its
 * DRAM through its DMA streams, voices set as the guide says, what they
 * make read back from its registers and its output.
 *
 *	emu8ktest
 *
 * Its interface (sixteen registers read back as written, the sample counter
 * at the sample rate, the detection's read-backs), its memory (DMA both
 * ways and both streams, the stale word first, the DRAM's 512 KiB
 * repeated, the ROM's area read only), the sound engine (an octave per
 * 0x1000 of pitch from 0xE000, the loop, the address one behind the
 * sound, the filter at Q 0 and cutoff 0xFF passing a voice bit for bit),
 * the envelopes (delays in 725 us units, the attack's 6 ms and 11.88 s,
 * the hold's 92 ms steps, the decay's 240 us/dB and 470 ms/dB within 6%,
 * the sustain's 0.75 dB steps, the release), the LFOs (0.042 Hz steps,
 * the depths of each modulation), the filter's resonances against the
 * ADIP's table, the pan, two voices' sum past 16 bits, the mute. Then a
 * passage of every feature at once, whose CRC every build must make
 * alike: the four made it 52095af5, which a change to the chip changes
 * too.
 */

enum
{
	D0 = 0x400,
	D1 = 0x800,
	D2 = 0x802,
	D3 = 0xc00,
	Ptr = 0xc02,
	Rate = 44100,
	Ram = 0x200000,
	Passage = 0x52095af5,	/* Linux and Windows, 386 and amd64, alike */
};

/* register, port: the guide's names */
#define CPF	0, D0
#define PTRX	1, D0
#define CVCF	2, D0
#define VTFT	3, D0
#define PSST	6, D0
#define CSL	7, D0
#define CCCA	0, D1
#define HWCF	1, D1
#define WC	1, D2
#define ENVVOL	4, D1
#define DCYSUSV	5, D1
#define ENVVAL	6, D1
#define DCYSUS	7, D1
#define ATKHLDV	4, D2
#define LFO1VAL	5, D2
#define ATKHLD	6, D2
#define LFO2VAL	7, D2
#define IP	0, D3
#define IFATN	1, D3
#define PEFE	2, D3
#define FMMOD	3, D3
#define TREMFRQ	4, D3
#define FM2FRQ2	5, D3

static const double pi = 3.14159265358979323846;
static int nchecks, nfails;
static int16_t out[2*Rate];
static int nout;
static uint32_t crctab[256];

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "emu8ktest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

static void
check(int ok, char *fmt, ...)
{
	va_list arg;

	nchecks++;
	if (ok)
		return;
	nfails++;
	va_start(arg, fmt);
	printf("emu8ktest: ");
	vprintf(fmt, arg);
	printf("\n");
	va_end(arg);
}

/* x within a fraction f of want */
static int
near(double x, double want, double f)
{
	return fabs(x - want) <= fabs(want) * f;
}

static int16_t
clip(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

/* The chip's 32-bit sum, cut to 16 bits as the card's codec took it. */
static void
sink(int32_t *o, int n)
{
	int i;

	for (i = 0; i < n && nout < Rate; i++, nout++) {
		out[2*nout] = clip(o[2*i]);
		out[2*nout+1] = clip(o[2*i+1]);
	}
}

static void
wr(int reg, int port, int ch, uint32_t v)
{
	emu8kout(Ptr, reg<<5 | ch);
	emu8kout(port, v & 0xffff);
}

static uint32_t
rd(int reg, int port, int ch)
{
	emu8kout(Ptr, reg<<5 | ch);
	return emu8kin(port);
}

/* a doubleword: the low word at the port, the high at the next */
static void
wrd(int reg, int port, int ch, uint32_t v)
{
	emu8kout(Ptr, reg<<5 | ch);
	emu8kout(port, v & 0xffff);
	emu8kout(port + 2, v >> 16);
}

static uint32_t
rdd(int reg, int port, int ch)
{
	uint32_t lo;

	emu8kout(Ptr, reg<<5 | ch);
	lo = emu8kin(port);
	return emu8kin(port + 2)<<16 | lo;
}

static void
run(uint32_t n)
{
	emu8krun(emu8kmade() + n);
}

/* The guide's initialisation, but the INIT arrays: configuration, channels, audio on. */
static void
initchip(void)
{
	int ch;

	emu8kreset(NULL, sink);
	nout = 0;
	wr(HWCF, 29, 0x59);
	wr(HWCF, 30, 0x20);
	for (ch = 0; ch < 32; ch++)
		wr(DCYSUSV, ch, 0x80);
	for (ch = 0; ch < 32; ch++) {
		wr(ENVVOL, ch, 0);
		wr(ENVVAL, ch, 0);
		wr(DCYSUS, ch, 0);
		wr(ATKHLDV, ch, 0);
		wr(LFO1VAL, ch, 0);
		wr(ATKHLD, ch, 0);
		wr(LFO2VAL, ch, 0);
		wr(IP, ch, 0);
		wr(IFATN, ch, 0);
		wr(PEFE, ch, 0);
		wr(FMMOD, ch, 0);
		wr(TREMFRQ, ch, 0);
		wr(FM2FRQ2, ch, 0);
		wrd(PTRX, ch, 0);
		wrd(VTFT, ch, 0);
		wrd(PSST, ch, 0);
		wrd(CSL, ch, 0);
		wrd(CCCA, ch, 0);
	}
	for (ch = 0; ch < 32; ch++) {
		wrd(CPF, ch, 0);
		wrd(CVCF, ch, 0);
	}
	wr(HWCF, 31, 4);
}

/* Channel ch given to a DMA stream, as the guide's section 5 says. */
static void
dmachannel(int ch, uint32_t mode)
{
	wr(DCYSUSV, ch, 0x80);
	wrd(VTFT, ch, 0);
	wrd(CVCF, ch, 0);
	wrd(PTRX, ch, 0x40000000);
	wrd(CPF, ch, 0x40000000);
	wrd(PSST, ch, 0);
	wrd(CSL, ch, 0);
	wrd(CCCA, ch, mode);
}

/* n words to sound memory at a, through the left or the right write stream. */
static void
dmawrite(uint32_t a, int16_t *w, int n, int right)
{
	int i;

	dmachannel(right ? 1 : 0, right ? 0x07000000 : 0x06000000);
	wrd(HWCF, right ? 23 : 22, a);
	for (i = 0; i < n; i++) {
		emu8kout(Ptr, 1<<5 | 26);
		emu8kout(right ? D2 : D1, (uint16_t)w[i]);
	}
	wrd(CCCA, right ? 1 : 0, 0);
}

/* n words from a, the stale one first, through a read stream. */
static void
dmaread(uint32_t a, int16_t *w, int n, int right, uint32_t *stale)
{
	int i;

	dmachannel(right ? 3 : 2, right ? 0x05000000 : 0x04000000);
	wrd(HWCF, right ? 21 : 20, a);
	*stale = rd(1, right ? D2 : D1, 26);
	for (i = 0; i < n; i++)
		w[i] = rd(1, right ? D2 : D1, 26);
	wrd(CCCA, right ? 3 : 2, 0);
}

static void
interface(void)
{
	static struct { int reg, port; } words[] = {
		{ENVVOL}, {DCYSUS}, {ENVVAL}, {ATKHLDV}, {LFO1VAL}, {ATKHLD}, {LFO2VAL},
		{IP}, {IFATN}, {PEFE}, {FMMOD}, {TREMFRQ}, {FM2FRQ2},
	};
	static struct { int reg, port; } dwords[] = {{PSST}, {CSL}, {CCCA}};
	uint32_t a, b, v;
	size_t i;
	int ch;

	initchip();
	for (ch = 0; ch < 32; ch += 7)
		for (i = 0; i < nelem(words); i++) {
			v = (0x1357 * (i + 1) + ch) & 0xff7f;
			wr(words[i].reg, words[i].port, ch, v);
			check(rd(words[i].reg, words[i].port, ch) == v, "word register %d/%x of %d: %04x, not %04x",
				words[i].reg, words[i].port, ch, rd(words[i].reg, words[i].port, ch), v);
		}
	for (ch = 0; ch < 32; ch += 7)
		for (i = 0; i < nelem(dwords); i++) {
			v = 0x00123456 + 0x01010101 * ch;
			if (dwords[i].port == D1)
				v &= 0xf3ffffff;
			wrd(dwords[i].reg, dwords[i].port, ch, v);
			check(rdd(dwords[i].reg, dwords[i].port, ch) == v, "doubleword register %d/%x of %d", dwords[i].reg,
				dwords[i].port, ch);
		}
	/* the sample counter: the samples made, 65536 in 1.486 s */
	a = rd(WC, 27);
	run(1000);
	b = rd(WC, 27);
	check(((b - a) & 0xffff) == 1000, "WC moved %u in 1000 samples", (b - a) & 0xffff);
	/* what A32GFX.DLL's detection reads */
	check((rd(7, D3, 0) & 0x0c) != 0, "register 7 of Data3: %04x", rd(7, D3, 0));
	wr(HWCF, 29, 0x59);
	check((rd(HWCF, 29) & 0x58) != 0, "HWCF1 reads %04x", rd(HWCF, 29));
	check((rd(HWCF, 30) & 1) != 0, "HWCF2 reads %04x", rd(HWCF, 30));
	wr(HWCF, 30, 0x20);
	check((rd(HWCF, 30) & 2) != 0, "HWCF2 after 0x20 reads %04x", rd(HWCF, 30));
	wr(HWCF, 31, 0);
	check((rd(HWCF, 30) & 0x40) == 0, "HWCF2 after HWCF3 0 reads %04x", rd(HWCF, 30));
	wr(HWCF, 31, 4);
}

static void
memory(void)
{
	int16_t w[64], r[64];
	uint32_t stale;
	int i;

	initchip();
	for (i = 0; i < 64; i++)
		w[i] = (int16_t)(i * 1031 - 30000);
	dmawrite(Ram + 0x100, w, 64, 0);
	dmaread(Ram + 0x100, r, 64, 0, &stale);
	check(memcmp(w, r, sizeof w) == 0, "the left streams: what was written not read back");
	dmaread(Ram + 0x100, r, 1, 1, &stale);
	check(r[0] == w[0], "the right read stream reads the left's writes");
	dmawrite(Ram + 0x400, w + 32, 32, 1);
	dmaread(Ram + 0x400, r, 32, 0, &stale);
	check(memcmp(w + 32, r, 32 * 2) == 0, "the right write stream");
	dmaread(Ram + 0x100, r, 4, 0, &stale);
	dmaread(Ram + 0x110, r, 1, 0, &stale);
	check(stale == (uint16_t)w[4], "the stale word: %04x, the last fetched %04x", stale, (uint16_t)w[4]);
	/* 512 KiB of DRAM, repeated above */
	dmaread(Ram + 0x40000 + 0x100, r, 4, 0, &stale);
	check(memcmp(w, r, 4 * 2) == 0, "the DRAM not repeated at 512 KiB");
	/* the ROM's area, the ROM NULL: silence, and writes there lost */
	dmawrite(0x1000, w, 4, 0);
	dmaread(0x1000, r, 4, 0, &stale);
	check(r[0] == 0 && r[3] == 0, "the ROM's area written");
	dmaread(0x100000, r, 4, 0, &stale);
	check(r[0] == 0, "between the ROM and the DRAM, not silence");
}

/* A voice playing sound memory from a, its loop from ls to le, at pitch p, its envelopes on. */
static void
voice(int ch, uint32_t a, uint32_t ls, uint32_t le, uint32_t ip, uint32_t ifatn, uint32_t pan)
{
	wr(DCYSUSV, ch, 0x80);
	wr(ENVVOL, ch, 0x8000);
	wr(ENVVAL, ch, 0x8000);
	wr(LFO1VAL, ch, 0x8000);
	wr(LFO2VAL, ch, 0x8000);
	wr(ATKHLDV, ch, 0x7f7f);
	wr(ATKHLD, ch, 0x7f7f);
	wr(DCYSUS, ch, 0x7f00);
	wr(IP, ch, ip);
	wr(IFATN, ch, ifatn);
	wr(PEFE, ch, 0);
	wr(FMMOD, ch, 0);
	wr(TREMFRQ, ch, 0);
	wr(FM2FRQ2, ch, 0);
	wrd(PTRX, ch, 0x40000000);
	wrd(CPF, ch, 0x40000000);
	wrd(CVCF, ch, 0x0000ffff);
	wrd(VTFT, ch, 0x0000ffff);
	wrd(PSST, ch, pan<<24 | ls);
	wrd(CSL, ch, le);
	wrd(CCCA, ch, a);
	wr(DCYSUSV, ch, 0x7f00);
}

/* A ramp of n words at a, each its offset. */
static void
ramp(uint32_t a, int n)
{
	int16_t w[256];
	int i;

	for (i = 0; i < n; i++)
		w[i] = i;
	dmawrite(a, w, n, 0);
}

static void
engine(void)
{
	uint32_t a0, a1, ca;
	int16_t w[256];
	int32_t x;
	int i, ok;

	initchip();
	ramp(Ram, 256);
	/* unity at 0xE000: a word a sample, and an octave up, two */
	voice(5, Ram, Ram, Ram + 250, 0xe000, 0xff00, 0xff);
	run(64);
	a0 = rdd(CCCA, 5) & 0xffffff;
	run(100);
	a1 = rdd(CCCA, 5) & 0xffffff;
	check(a1 - a0 == 100, "at unity the address moved %u in 100 samples", a1 - a0);
	wr(IP, 5, 0xf000);
	run(64);
	a0 = rdd(CCCA, 5) & 0xffffff;
	run(50);
	a1 = rdd(CCCA, 5) & 0xffffff;
	check(a1 - a0 == 100, "an octave up the address moved %u in 50 samples", a1 - a0);
	/* the loop: past the end, back by its length */
	wr(IP, 5, 0xe000);
	wrd(CCCA, 5, Ram + 200);
	run(80);
	ca = rdd(CCCA, 5) & 0xffffff;
	check(ca >= Ram && ca < Ram + 250, "the loop: the address at %x", ca);
	/*
	 * the sound one word ahead of the address, and the filter at Q 0 and
	 * cutoff 0xFF passing it bit for bit: at unity and full volume, panned
	 * left, the output is each word times 0xffff/0x10000
	 */
	initchip();
	for (i = 0; i < 256; i++)
		w[i] = (int16_t)(sin(i * 0.1) * 20000);
	dmawrite(Ram, w, 256, 0);
	voice(3, Ram, Ram, Ram + 250, 0xe000, 0xff00, 0xff);
	run(Rate/10);
	ca = rdd(CCCA, 3) & 0xffffff;
	nout = 0;
	run(300);
	ok = 1;
	for (i = 0; i < 290; i++) {
		x = w[(ca - Ram + i) % 250 + 1];
		x = x >= 0 ? x * 0xffff >> 16 : -((-x * 0xffff - 1) >> 16) - 1;
		if (out[2*i] != x || out[2*i+1] != 0)
			ok = 0;
	}
	check(ok, "Q 0 and cutoff 0xff do not pass the sound as it is");
}

/* The volume target in dB below full, from VTFT's high word. */
static double
vtdb(int ch)
{
	uint32_t v;

	v = rdd(VTFT, ch) >> 16;
	return v == 0 ? -200 : 20 * log10(v / 65535.0);
}

/* A note started again on ch: its targets cleared, its delay, attack and hold, its decay and sustain. */
static void
note(int ch, uint32_t envvol, uint32_t atkhldv, uint32_t dcysusv)
{
	wr(DCYSUSV, ch, 0x80);
	wrd(VTFT, ch, 0);
	wrd(CVCF, ch, 0);
	wr(ENVVOL, ch, envvol);
	wr(ATKHLDV, ch, atkhldv);
	wr(DCYSUSV, ch, dcysusv);
}

/* Samples until the volume target reaches 0 dB, after the note's start, at most n. */
static uint32_t
untilfull(int ch, uint32_t n)
{
	uint32_t t0;

	t0 = emu8kmade();
	while (emu8kmade() - t0 < n) {
		if ((rdd(VTFT, ch) >> 16) == 0xffff)
			return emu8kmade() - t0;
		run(32);
	}
	return n;
}

static void
envelopes(void)
{
	uint32_t t, t0;
	double d0, d1, s;
	int i;

	initchip();
	ramp(Ram, 256);
	/* attack 0x7f: 6 ms, 0x01: 11.88 s */
	voice(2, Ram, Ram, Ram + 250, 0xe000, 0xff00, 0x80);
	note(2, 0x8000, 0x7f7f, 0x7f00);
	t = untilfull(2, Rate);
	check(near(t / (double)Rate, 0.006, 0.12), "attack 0x7f takes %.4f s", t / (double)Rate);
	note(2, 0x8000, 0x7f01, 0x7f00);
	t = untilfull(2, 13*Rate);
	check(near(t / (double)Rate, 11.88, 0.01), "attack 0x01 takes %.3f s", t / (double)Rate);
	/* a delay of 100 units of 725 us before the attack */
	note(2, 0x8000 - 100, 0x7f7f, 0x7f00);
	t = untilfull(2, Rate);
	check(near(t / (double)Rate, 0.0725 + 0.006, 0.05), "a delay of 100 and attack 0x7f take %.4f s", t / (double)Rate);
	/* hold: 0x7e, 92 ms at full, then the decay */
	note(2, 0x8000, 0x7e7f, 0x007f);
	untilfull(2, Rate);
	t0 = emu8kmade();
	while (emu8kmade() - t0 < Rate && (rdd(VTFT, 2) >> 16) == 0xffff)
		run(32);
	t = emu8kmade() - t0;
	check(near(t / (double)Rate, 0.092, 0.05), "hold 0x7e lasts %.4f s", t / (double)Rate);
	/* decay 0x7f: 240 us/dB, 0x01: 470 ms/dB (the model's ladder 3.6% and 5.0% slower) */
	run(32*4);
	d0 = vtdb(2);
	run(32*8);
	d1 = vtdb(2);
	s = (32*8 / (double)Rate) / (d0 - d1);
	check(near(s, 0.000240, 0.06), "decay 0x7f: %.6f s/dB", s);
	note(2, 0x8000, 0x7f7f, 0x0001);
	untilfull(2, Rate);
	run(Rate);
	d0 = vtdb(2);
	run(4*Rate);
	d1 = vtdb(2);
	s = 4.0 / (d0 - d1);
	check(near(s, 0.470, 0.06), "decay 0x01: %.4f s/dB", s);
	/* sustain: 0.75 dB steps, 0x7f at 0 dB */
	for (i = 0x7f; i >= 0x60; i -= 0x0f) {
		note(2, 0x8000, 0x7f7f, i<<8 | 0x7f);
		run(Rate/4);
		check(near(vtdb(2), -0.75 * (0x7f - i), 0.02) || (i == 0x7f && vtdb(2) == 0),
			"sustain %02x at %.2f dB", i, vtdb(2));
	}
	/* release: bit 15, at its rate from where it is */
	wr(DCYSUSV, 2, 0x807f);
	run(32*8);
	d0 = vtdb(2);
	run(32*8);
	d1 = vtdb(2);
	s = (32*8 / (double)Rate) / (d0 - d1);
	check(near(s, 0.000240, 0.06), "release 0x7f: %.6f s/dB", s);
}

/* The pitch target in units of 1/4096 octave, from PTRX's high word. */
static double
ptoct(int ch)
{
	uint32_t p;

	p = rdd(PTRX, ch) >> 16;
	return p == 0 ? -100 : 4096 * log2(p / 16384.0);
}

/* The extremes of f over n frames of channel ch, from its next frame. */
static void
extremes(double (*f)(int), int ch, uint32_t n, double *lo, double *hi)
{
	uint32_t i;
	double x;

	*lo = 1e9;
	*hi = -1e9;
	for (i = 0; i < n; i++) {
		run(32);
		x = f(ch);
		if (x < *lo)
			*lo = x;
		if (x > *hi)
			*hi = x;
	}
}

/* The LFO's period, in frames, from the pitch target's crossings of the note's pitch. */
static double
lfoperiod(int ch, uint32_t frames)
{
	uint32_t i, n, first, last;
	double x, prev;

	n = first = last = 0;
	prev = ptoct(ch);
	for (i = 0; i < frames; i++) {
		run(32);
		x = ptoct(ch);
		if (prev < 0 && x >= 0) {
			if (n == 0)
				first = i;
			last = i;
			n++;
		}
		prev = x;
	}
	return n < 2 ? 0 : (last - first) / (double)(n - 1);
}

/* The cutoff target in octaves above 125 Hz, from VTFT's low word. */
static double
fcoct(int ch)
{
	return (rdd(VTFT, ch) & 0xffff) / 256.0 * 6 / 256;
}

static void
lfos(void)
{
	double lo, hi, f;

	initchip();
	ramp(Ram, 256);
	voice(4, Ram, Ram, Ram + 250, 0xe000, 0x80ff, 0x80);
	/* LFO 1 at 0x40: 64 steps of 0.042 Hz, its vibrato 0x7f, an octave */
	wr(TREMFRQ, 4, 0x0040);
	wr(FMMOD, 4, 0x7f00);
	f = Rate / 32.0 / lfoperiod(4, 4000);
	check(near(f, 64 * 0.042, 0.02), "LFO 1 at 0x40: %.3f Hz", f);
	extremes(ptoct, 4, 1100, &lo, &hi);
	check(near(hi, 4096, 0.02) && near(lo, -4096, 0.02), "vibrato 0x7f: %.0f to %.0f", lo, hi);
	wr(TREMFRQ, 4, 0x00ff);
	f = Rate / 32.0 / lfoperiod(4, 2000);
	check(near(f, 10.72, 0.02), "LFO 1 at 0xff: %.3f Hz", f);
	/* -1 octave at 0x80 */
	wr(FMMOD, 4, 0x8000);
	extremes(ptoct, 4, 600, &lo, &hi);
	check(near(hi, 4096, 0.02) && near(lo, -4096, 0.02), "vibrato 0x80: %.0f to %.0f", lo, hi);
	/* its filter modulation: 3 octaves at 0x7f */
	wr(FMMOD, 4, 0x007f);
	extremes(fcoct, 4, 600, &lo, &hi);
	check(near(hi - lo, 6, 0.02), "LFO 1 to the filter 0x7f: %.2f octaves peak to peak", hi - lo);
	/* its tremolo: 12 dB at 0x7f */
	wr(FMMOD, 4, 0);
	wr(IFATN, 4, 0x8000 | 64);
	wr(TREMFRQ, 4, 0x7fff);
	extremes(vtdb, 4, 600, &lo, &hi);
	check(near(hi - lo, 24, 0.02), "tremolo 0x7f: %.2f dB peak to peak", hi - lo);
	/* LFO 2: its vibrato, an octave at 0x7f */
	wr(TREMFRQ, 4, 0);
	wr(FM2FRQ2, 4, 0x7f80);
	extremes(ptoct, 4, 600, &lo, &hi);
	check(near(hi, 4096, 0.02) && near(lo, -4096, 0.02), "LFO 2's vibrato 0x7f: %.0f to %.0f", lo, hi);
	f = Rate / 32.0 / lfoperiod(4, 2000);
	check(near(f, 128 * 0.042, 0.02), "LFO 2 at 0x80: %.3f Hz", f);
	/* the modulation envelope: +1 octave of pitch at 0x7f, +6 of cutoff */
	wr(FM2FRQ2, 4, 0);
	wr(DCYSUSV, 4, 0x80);
	wr(IFATN, 4, 0x0000);
	wr(PEFE, 4, 0x7f7f);
	wr(ATKHLD, 4, 0x7f7f);
	wr(DCYSUS, 4, 0x7f00);
	wr(DCYSUSV, 4, 0x7f00);
	run(Rate/10);
	check(near(ptoct(4), 4096, 0.02), "envelope to pitch 0x7f: %.0f", ptoct(4));
	check(near(fcoct(4), 6, 0.02), "envelope to the filter 0x7f: %.2f octaves", fcoct(4));
}

/* The table's filter of a Q and cutoff: its gain, f and q, and its feedbacks as a two-pole's. */
static void
filter(int q, int c, double *g, double *f, double *a1, double *a2)
{
	double qq;

	*g = emu8kfilter[q][c][0] / (double)(1<<28);
	*f = emu8kfilter[q][c][1] / (double)(1<<28);
	qq = emu8kfilter[q][c][2] / (double)(1<<28);
	*a1 = -(2 - *f*qq - *f * *f);
	*a2 = 1 - *f*qq;
}

/* Its gain at w (radians a sample), in dB: g f^2 z^-1 / (1 + a1 z^-1 + a2 z^-2). */
static double
fgain(int q, int c, double w)
{
	double g, f, a1, a2, re, im;

	filter(q, c, &g, &f, &a1, &a2);
	re = 1 + a1*cos(w) + a2*cos(2*w);
	im = -a1*sin(w) - a2*sin(2*w);
	return 20 * log10(g * f * f / hypot(re, im));
}

/* Its highest gain: at the frequency its poles give, and about it. */
static double
fpeak(int q, int c)
{
	double g, f, a1, a2, r, ct, cw, w, x, best;
	int i;

	filter(q, c, &g, &f, &a1, &a2);
	r = sqrt(a2);
	ct = -a1 / (2 * r);
	cw = (1 + r*r) * ct / (2 * r);
	w = cw >= 1 ? 0 : acos(cw);
	best = fgain(q, c, 0);
	for (i = -200; i <= 200; i++) {
		x = w * (1 + i / 2000.0);
		if (x > 0 && x < pi && fgain(q, c, x) > best)
			best = fgain(q, c, x);
	}
	return best;
}

static void
filters(void)
{
	static double lowq[16] = {5, 6, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23, 25, 26, 28};
	static double highq[16] = {0, 0.5, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 13, 15, 16, 18};
	static double dc[16] = {0, -0.5, -1.2, -1.8, -2.5, -3.3, -4.1, -5.5, -6.0, -6.6, -7.2, -7.9, -8.5, -9.3, -10.1, -11.0};
	double peak;
	int q, c;

	check(emu8kfilter[0][255][1] == 0, "Q 0 and cutoff 0xff not the identity");
	for (q = 0; q < 16; q++)
		for (c = 0; c < 256; c += 255) {
			if (q == 0 && c == 255)
				continue;
			peak = fpeak(q, c);
			check(fabs(fgain(q, c, 0) - dc[q]) < 0.1, "Q %d cutoff %d: DC at %.2f dB, not %.1f", q, c,
				fgain(q, c, 0), dc[q]);
			check(fabs(peak - fgain(q, c, 0) - (c == 0 ? lowq[q] : highq[q])) < 0.5,
				"Q %d cutoff %d: the peak %.2f dB above DC, not %.1f", q, c, peak - fgain(q, c, 0),
				c == 0 ? lowq[q] : highq[q]);
		}
}

static void
outputs(void)
{
	int16_t w[256];
	int i, l, r, maxl;

	initchip();
	for (i = 0; i < 256; i++)
		w[i] = i < 128 ? 30000 : -30000;
	dmawrite(Ram, w, 256, 0);
	/* pan 0: the right alone, 0xff: the left alone */
	voice(6, Ram, Ram, Ram + 250, 0xe000, 0xff00, 0);
	run(64);
	nout = 0;
	run(256);
	l = r = 0;
	for (i = 0; i < 256; i++) {
		l |= out[2*i] != 0;
		r |= out[2*i+1] != 0;
	}
	check(!l && r, "pan 0 is not the right alone");
	wrd(PSST, 6, 0xff000000 | Ram);
	run(64);
	nout = 0;
	run(256);
	l = r = 0;
	for (i = 0; i < 256; i++) {
		l |= out[2*i] != 0;
		r |= out[2*i+1] != 0;
	}
	check(l && !r, "pan 0xff is not the left alone");
	/* two voices at full, in step: their sum past 16 bits, the codec's clip */
	voice(7, rdd(CCCA, 6) & 0xffffff, Ram, Ram + 250, 0xe000, 0xff00, 0xff);
	run(Rate/10);
	nout = 0;
	run(256);
	maxl = 0;
	for (i = 0; i < 256; i++)
		if (abs(out[2*i]) > maxl)
			maxl = abs(out[2*i]);
	check(maxl >= 32767, "two voices at full reach %d", maxl);
	/* HWCF3 0: silence */
	wr(HWCF, 31, 0);
	nout = 0;
	run(256);
	l = 0;
	for (i = 0; i < 256; i++)
		l |= out[2*i] | out[2*i+1];
	check(!l, "HWCF3 0 is not silence");
	wr(HWCF, 31, 4);
}

uint32_t
crc(uint32_t c, const uint8_t *p, size_t n)
{
	c = ~c;
	while (n-- > 0)
		c = crctab[(c ^ *p++) & 0xff] ^ c>>8;
	return ~c;
}

/*
 * A second of every feature at once: eight voices of three waves, each
 * its own envelopes, LFOs, filter, pan and sends, the chorus set by the
 * words of the INIT arrays that set it and HWCF5. Its CRC is what this
 * build makes of it.
 */
static uint32_t
passage(void)
{
	int16_t w[512];
	uint32_t c;
	int i, ch, k;

	initchip();
	for (ch = 9; ch <= 13; ch += 1)
		if (ch == 9 || ch == 10 || ch == 13)
			wrd(HWCF, ch, ch == 10 ? 0x83 : ch == 13 ? 0x8000 : 0);
	/* the chorus: its feedback, delay and depth */
	wr(3, D1, 9, 0x2000);
	wr(3, D1, 12, 0x0300);
	wr(3, D2, 3, 0x0800);
	/* a triangle and a saw, in integers: the same on every system */
	for (i = 0; i < 512; i++)
		w[i] = (int16_t)((i % 64 < 32 ? i % 64 : 64 - i % 64) * 750 - 12000 + (i % 9) * 700 * (i % 3 == 0));
	dmawrite(Ram, w, 512, 0);
	for (k = 0; k < 8; k++) {
		ch = 3*k + 1;
		voice(ch, Ram + 64*(k % 3), Ram + 64*(k % 3), Ram + 64*(k % 3) + 60, 0xd000 + 0x200*k,
			(0x40 + 0x18*k)<<8 | 4*k, 0x20*k);
		wr(DCYSUSV, ch, 0x80);
		wr(ENVVOL, ch, 0x8000 - 10*k);
		wr(ATKHLDV, ch, (0x70 + k)<<8 | (0x50 + 6*k));
		wr(ENVVAL, ch, 0x8000);
		wr(ATKHLD, ch, 0x7f40);
		wr(DCYSUS, ch, 0x4020);
		wr(PEFE, ch, (k*10)<<8 | (0x80 + 9*k));
		wr(FMMOD, ch, (k*3)<<8 | (k*5));
		wr(TREMFRQ, ch, (k*4)<<8 | (0x20 + 8*k));
		wr(FM2FRQ2, ch, (0x100 - k*2)<<8 | (0x30 + k));
		wrd(PTRX, ch, 0x40000000 | (0x10*k)<<8);
		wrd(CSL, ch, (uint32_t)(0x18*k)<<24 | (Ram + 64*(k % 3) + 60));
		wrd(CCCA, ch, (uint32_t)(2*k)<<28 | (Ram + 64*(k % 3)));
		wr(DCYSUSV, ch, (0x60 + k)<<8 | (0x30 + 4*k));
	}
	nout = 0;
	for (i = 0; i < 4; i++) {
		run(Rate/4);
		wr(DCYSUSV, 3*i + 1, 0x8000 | (0x20 + 8*i));
	}
	c = crc(0, (uint8_t*)out, sizeof out);
	return c;
}

int
main(void)
{
	uint32_t c, crcs[2];
	int i, k;

	for (i = 0; i < 256; i++) {
		c = i;
		for (k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320 ^ c>>1 : c>>1;
		crctab[i] = c;
	}
	interface();
	memory();
	engine();
	envelopes();
	lfos();
	filters();
	outputs();
	crcs[0] = passage();
	crcs[1] = passage();
	check(crcs[0] == crcs[1], "the passage made twice differs");
	check(crcs[0] == Passage, "the passage's CRC %08x, not %08x as the four builds made it", crcs[0], Passage);
	printf("emu8ktest: %d checks, %d failed; the passage's CRC %08x\n", nchecks, nfails, crcs[0]);
	return nfails != 0;
}
