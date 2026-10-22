#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The EMU8000's effects. The chip's reverb, chorus and equalizer come
 * from a DSP its INIT arrays program, with a DRAM of its own for the
 * delays. The documents give the arrays alone, so this models what the
 * effects do: a reverb of eight combs damped in their feedback and four
 * all-passes, left and right a little apart, Freeverb's network with
 * its own gains (its input times 0.015, its output whole), and a chorus
 * of a delay swept by a triangle, with feedback. The equalizer is flat.
 * The chorus takes its words where the Linux driver writes a chorus
 * mode's (INIT3 9 its feedback, INIT3 12 its delay, INIT4 3 its depth,
 * HWCF5 its rate, in milliHertz). The reverb is a hall's, whatever its
 * 28 words. In integers.
 */

enum
{
	Ncomb = 8,
	Nap = 4,
	Spread = 23,
	Chorusmax = 8192,
	Combmax = 1617 + Spread,
	Apmax = 556 + Spread,
};

static int16_t combs[Ncomb] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static int16_t aps[Nap] = {556, 441, 341, 225};

static int32_t cbuf[2][Ncomb][Combmax];
static int32_t cstore[2][Ncomb];
static int32_t abuf[2][Nap][Apmax];
static int cpos[2][Ncomb], apos[2][Nap];
static int32_t chorus[Chorusmax];
static int chpos;
static uint32_t chphase;	/* Q32 */

/* the parameters: Q15 gains, samples, a Q32 step */
static int32_t fixedgain = 492;	/* Freeverb's 0.015 */
static int32_t room = 28180;	/* 0.86: a hall's */
static int32_t damp = 6554;	/* 0.2 */
static int32_t chfeedback;
static int32_t chdelay = 1014;
static int32_t chdepth = 240;
static uint32_t chstep;

/* x times g, Q15, rounded toward 0, so that a loop's feedback dies out. */
static int32_t
mulq15(int32_t x, int32_t g)
{
	int64_t p;

	p = (int64_t)x * g;
	return (int32_t)(p >= 0 ? p >> 15 : -(-p >> 15));
}

/* emu8kfxreset: the effects silent. */
void
emu8kfxreset(void)
{
	memset(cbuf, 0, sizeof cbuf);
	memset(cstore, 0, sizeof cstore);
	memset(abuf, 0, sizeof abuf);
	memset(cpos, 0, sizeof cpos);
	memset(apos, 0, sizeof apos);
	memset(chorus, 0, sizeof chorus);
	chpos = 0;
	chphase = 0;
}

/* emu8kfxinit: the parameters, from the INIT arrays' words and HWCF5. */
void
emu8kfxinit(uint16_t (*init)[32], uint32_t hwcf5)
{
	chfeedback = (int16_t)init[2][9];
	chdelay = init[2][12] & 0x0fff;
	chdepth = (init[3][3] & 0x7fff) >> 6;
	/* HWCF5 in milliHertz: a triangle's period, a step of 2^32/period */
	chstep = (uint32_t)((uint64_t)(hwcf5 & 0xffff) * 0x100000000ull / (44100ull * 1000));
}

/* The reverb of one side's input, Freeverb's network. */
static int32_t
reverb(int s, int32_t x)
{
	int32_t out, y, *b;
	int i, n;

	x = mulq15(x, fixedgain);
	out = 0;
	for (i = 0; i < Ncomb; i++) {
		n = combs[i] + s*Spread;
		b = &cbuf[s][i][cpos[s][i]];
		y = *b;
		cstore[s][i] = mulq15(y, 32768 - damp) + mulq15(cstore[s][i], damp);
		*b = x + mulq15(cstore[s][i], room);
		if (++cpos[s][i] >= n)
			cpos[s][i] = 0;
		out += y;
	}
	for (i = 0; i < Nap; i++) {
		n = aps[i] + s*Spread;
		b = &abuf[s][i][apos[s][i]];
		y = *b;
		*b = out + y/2;
		out = y - out;
		if (++apos[s][i] >= n)
			apos[s][i] = 0;
	}
	return out;
}

/* The chorus: the line read behind its write by the delay and the swept depth, with feedback. */
static int32_t
chorusout(int32_t x)
{
	int32_t tri, d, a, b, y;
	uint32_t p;
	int i, j;

	p = chphase >> 17;
	tri = p < 0x4000 ? (int32_t)p : 0x8000 - (int32_t)p;
	d = chdelay * 256 + (chdepth * tri >> 6);
	i = (chpos - (d >> 8) + 2*Chorusmax) % Chorusmax;
	j = (i - 1 + Chorusmax) % Chorusmax;
	a = chorus[i];
	b = chorus[j];
	y = a + (int32_t)((int64_t)(b - a) * (d & 0xff) / 256);
	chorus[chpos] = x + mulq15(y, chfeedback);
	chpos = (chpos + 1) % Chorusmax;
	chphase += chstep;
	return y;
}

/* emu8kfx: a sample's reverb and chorus sends made, their returns added to the left and right. */
void
emu8kfx(int32_t rev, int32_t cho, int32_t *l, int32_t *r)
{
	int32_t c;

	*l += reverb(0, rev);
	*r += reverb(1, rev);
	c = chorusout(cho);
	*l += c;
	*r += c;
}
