#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The EMU8000, the AWE32's synthesizer, as E-mu's programmer's guide
 * and Creative's developer's information pack give it: its registers at
 * its five ports (Data0 at 0x400, Data1 0x800, Data2 0x802, Data3
 * 0xC00, the pointer 0xC02, from the base), its sound memory (the 1 MB
 * ROM, 512 KiB of DRAM from 0x200000), its DMA streams and its sample
 * counter, and its sound at 44100 Hz: each sample one channel's
 * envelope engine (its envelopes, its LFOs, the targets it writes into
 * PTRX and VTFT), then every voice's oscillator, filter, amplifier, pan
 * and sends, then the effects (emu8kfx.c), and out their sum in 32
 * bits, which the card's mixer scaled before its codec cut it to 16. In
 * integers only, so that every build makes the same samples. What the
 * documents leave open is modelled: the effects, the slews, the
 * interpolation's kernel, the filter's shape between the points they
 * give, the output's level.
 *
 * The chip keeps its own time, in 1/65536 sample: a word access costs a
 * microsecond of it, the samples before it made first, so that the
 * driver's polling of the sample counter sees every value.
 */

enum
{
	Nchan = 32,
	Romwords = 0x80000,
	Ramwords = 0x40000,	/* 512 KiB */
	Ramstart = 0x200000,
	Fmstart = 0xffffe0,
	Tio = 2890,		/* a word access: 1 us */

	Pdata0 = 0x400,
	Pdata0h = 0x402,
	Pdata1 = 0x800,
	Pdata2 = 0x802,
	Pdata3 = 0xc00,
	Ppointer = 0xc02,

	/* an envelope's phases */
	Edelay = 0,
	Eattack,
	Ehold,
	Edecay,
	Erelease,
};

typedef struct Env Env;
typedef struct Voice Voice;

struct Env
{
	int	phase;
	int32_t	amp;		/* the attack's, linear, 0 to 0xffff */
	int32_t	att;		/* past it, attenuation in 1/4096 octave, to 0xffff */
	int32_t	hold;		/* frames left */
};

struct Voice
{
	uint32_t	cpf, ptrx, cvcf, vtft, psst, csl, ccca;
	uint32_t	z2, z1;		/* Data0 4 and 5: the filter's state */
	uint16_t	envvol, dcysusv, envval, dcysus;
	uint16_t	atkhldv, lfo1val, atkhld, lfo2val;
	uint16_t	ip, ifatn, pefe, fmmod, tremfrq, fm2frq2;
	uint32_t	r1;		/* register 1 of Data1 and Data2: HWCF4 to 6, the streams' */
	Env	vol, mod;
	uint16_t	lfo1, lfo2;	/* 15-bit phases */
	int	k;		/* samples since its envelope engine's frame */
};

static Voice voice[Nchan];
static uint16_t init[4][Nchan];
static uint16_t hwcf1, hwcf2, hwcf3, u7;
static uint16_t ptr;
static uint16_t smld, smrd;	/* the read streams' words, fetched by the read before */
static uint64_t t;		/* in 1/65536 sample */
static uint64_t made;		/* samples */
static uint16_t *rom;
static uint16_t ram[Ramwords];
static void (*sink)(int32_t*, int);

/* A word of sound memory. */
static int32_t
mem(uint32_t a)
{
	a &= 0xffffff;
	if (a < Romwords)
		return rom != NULL ? (int16_t)rom[a] : 0;
	if (a < Ramstart || a >= Fmstart)
		return 0;
	return (int16_t)ram[(a - Ramstart) % Ramwords];
}

static void
memwrite(uint32_t a, uint32_t v)
{
	a &= 0xffffff;
	if (a >= Ramstart && a < Fmstart)
		ram[(a - Ramstart) % Ramwords] = v;
}

/* x shifted right by n, rounded down: an arithmetic shift, defined for negatives. */
static int32_t
sar(int32_t x, int n)
{
	return x >= 0 ? x >> n : -((-(x + 1) >> n) + 1);
}

static int64_t
sar64(int64_t x, int n)
{
	return x >= 0 ? x >> n : -((-(x + 1) >> n) + 1);
}

/* 2^(x/4096), x in 1/4096 octave, 0 to 16 octaves. */
static uint32_t
pow2(int32_t x)
{
	if (x < 0)
		return 0;
	return (uint32_t)(((uint64_t)emu8kexp2[x & 0xfff] << (x >> 12)) >> 16);
}

/* 0xffff attenuated by a, in 1/4096 octave. */
static uint32_t
level(int32_t a)
{
	if (a <= 0)
		return 0xffff;
	if (a >= 0x10000)
		return 0;
	return (uint32_t)((uint64_t)pow2(0x10000 - a) * 0xffff >> 16);
}

/* An amplitude of 0 to 0xffff as an attenuation, in 1/4096 octave, at most 0xffff. */
static int32_t
atten(int32_t amp)
{
	int32_t e, a;

	if (amp <= 0)
		return 0xffff;
	for (e = 15; (amp >> e) == 0; e--)
		;
	a = 4096*(16 - e) - emu8klog2[((amp << (15 - e)) - 0x8000) >> 3];
	return a > 0xffff ? 0xffff : a;
}

/*
 * A rate code: 0 to 15 as they are, then four bits of mantissa and
 * three of exponent, to 1984.
 */
static int32_t
rate(uint32_t c)
{
	c &= 0x7f;
	if (c < 16)
		return c;
	return (16 + (c & 15)) << ((c >> 4) - 1);
}

/* An LFO's triangle, from 0 upward, a 15-bit phase: -32767 to 32767. */
static int32_t
tri(uint32_t p)
{
	int32_t v;

	p &= 0x7fff;
	if (p < 0x2000)
		v = p * 4;
	else if (p < 0x6000)
		v = (0x4000 - (int32_t)p) * 4;
	else
		v = ((int32_t)p - 0x8000) * 4;
	if (v > 32767)
		v = 32767;
	if (v < -32767)
		v = -32767;
	return v;
}

/*
 * An envelope's frame: its delay counted up to 0x8000 in its register,
 * its attack in amplitude, then its hold, its decay to the sustain and
 * its release in attenuation.
 */
static void
envframe(Env *e, uint16_t *delay, uint16_t *atkhld, uint16_t dcy)
{
	int32_t sus;

	if (e->phase == Erelease) {
		e->att += rate(dcy);
		if (e->att > 0xffff)
			e->att = 0xffff;
		return;
	}
	switch (e->phase) {
	case Edelay:
		if ((*delay & 0x8000) == 0) {
			(*delay)++;
			return;
		}
		e->phase = Eattack;
		/* fall through */
	case Eattack:
		if ((*atkhld & 0x7f) != 0)
			e->amp += 4 * rate(*atkhld);
		if (e->amp < 0xffff) {
			e->att = atten(e->amp);
			return;
		}
		e->amp = 0xffff;
		e->att = 0;
		*atkhld |= 0x8000;
		e->hold = (0x7f - (*atkhld >> 8 & 0x7f)) * 128;
		e->phase = Ehold;
		/* fall through */
	case Ehold:
		if (e->hold > 0) {
			e->hold--;
			return;
		}
		e->phase = Edecay;
		/* fall through */
	case Edecay:
		sus = (0x7f - (dcy >> 8 & 0x7f)) << 9;
		if ((dcy & 0x7f) == 0 || e->att >= sus)
			return;
		e->att += rate(dcy);
		if (e->att > sus)
			e->att = sus;
		return;
	}
}

/* An envelope released: from where its attack was, if there. */
static void
release(Env *e)
{
	if (e->phase == Edelay)
		e->att = 0xffff;
	else if (e->phase == Eattack)
		e->att = atten(e->amp);
	e->phase = Erelease;
}

static void
start(Voice *v)
{
	v->vol.phase = v->mod.phase = Edelay;
	v->vol.amp = v->mod.amp = 0;
	v->vol.att = v->mod.att = 0xffff;
	v->lfo1 = v->lfo2 = 0;
}

/*
 * The envelope engine's frame of a channel: its LFOs and envelopes
 * stepped, its targets written: PTRX's pitch, VTFT's volume and cutoff.
 */
static void
engine(Voice *v)
{
	int32_t l1, l2, m, x, a, f;
	uint32_t vt, pt;

	if (v->dcysusv & 0x80)
		return;
	l1 = l2 = 0;
	if (v->lfo1val & 0x8000) {
		l1 = tri(v->lfo1);
		v->lfo1 = (v->lfo1 + (v->tremfrq & 0xff)) & 0x7fff;
	} else
		v->lfo1val++;
	if (v->lfo2val & 0x8000) {
		l2 = tri(v->lfo2);
		v->lfo2 = (v->lfo2 + (v->fm2frq2 & 0xff)) & 0x7fff;
	} else
		v->lfo2val++;
	envframe(&v->vol, &v->envvol, &v->atkhldv, v->dcysusv);
	envframe(&v->mod, &v->envval, &v->atkhld, v->dcysus);
	m = 0xffff - v->mod.att;
	x = v->ip + sar((int8_t)(v->pefe >> 8) * m, 11) + sar((int8_t)(v->fmmod >> 8) * l1, 10)
		+ sar((int8_t)(v->fm2frq2 >> 8) * l2, 10);
	pt = pow2(x);
	if (pt > 0xffff)
		pt = 0xffff;
	a = v->vol.att + ((v->ifatn & 0xff) << 8) - sar((int8_t)(v->tremfrq >> 8) * l1, 9);
	vt = level(a);
	f = (v->ifatn & 0xff00) + sar((int8_t)v->pefe * m, 7) + sar((int8_t)v->fmmod * l1, 7);
	if (f < 0)
		f = 0;
	if (f > 0xffff)
		f = 0xffff;
	v->ptrx = pt<<16 | (v->ptrx & 0xffff);
	v->vtft = vt<<16 | f;
}

/*
 * A current value a sample nearer its target: the gap's share of the
 * samples left in the frame, so that it gets there by the frame's end.
 */
static int32_t
slew(int32_t cur, int32_t to, int k)
{
	return cur + (to - cur) / (32 - k);
}

/*
 * A voice's sample: the oscillator read at its address through the
 * four-point kernel, the address stepped by the current pitch and
 * looped, the filter, the volume. It goes to its sends.
 */
static void
voicesample(Voice *v, int32_t *l, int32_t *r, int32_t *rev, int32_t *cho)
{
	uint32_t ca, f, ls, le, q, c, pan;
	uint64_t pos;
	int32_t x, y, out, cp, cv, cf, in, lp, hp;
	int16_t *k0, *k1;
	int32_t *co;
	int i, fr;

	if (v->ccca & 0x04000000)
		return;
	cp = slew(v->cpf >> 16, v->ptrx >> 16, v->k);
	cv = slew(v->cvcf >> 16, v->vtft >> 16, v->k);
	cf = slew(v->cvcf & 0xffff, v->vtft & 0xffff, v->k);
	if (v->k < 31)
		v->k++;
	ca = v->ccca & 0xffffff;
	f = v->cpf & 0xffff;
	k0 = emu8kkernel[f >> 8];
	k1 = emu8kkernel[(f >> 8) + 1];
	fr = f & 0xff;
	x = 0;
	for (i = 0; i < 4; i++)
		x += mem(ca + i) * (k0[i] + sar((k1[i] - k0[i]) * fr, 8));
	x = sar(x, 14);
	pos = ((uint64_t)ca << 16 | f) + ((uint64_t)(uint32_t)cp << 2);
	ca = pos >> 16 & 0xffffff;
	f = pos & 0xffff;
	ls = v->psst & 0xffffff;
	le = v->csl & 0xffffff;
	if (ca >= le)
		ca = (ca - (le - ls)) & 0xffffff;
	v->ccca = (v->ccca & 0xff000000) | ca;
	v->cpf = (uint32_t)cp<<16 | f;
	v->cvcf = (uint32_t)cv<<16 | (uint32_t)cf;
	q = v->ccca >> 28;
	c = (uint32_t)cf >> 8;
	co = emu8kfilter[q][c];
	if (co[1] == 0)
		y = x * 256;
	else {
		/* a state-variable filter, its low-pass in Z1, its band-pass in Z2 */
		in = (int32_t)sar64((int64_t)co[0] * (x * 256), 28);
		lp = (int32_t)v->z1 + (int32_t)sar64((int64_t)co[1] * (int32_t)v->z2, 28);
		hp = in - lp - (int32_t)sar64((int64_t)co[2] * (int32_t)v->z2, 28);
		v->z2 = (int32_t)v->z2 + (int32_t)sar64((int64_t)co[1] * hp, 28);
		v->z1 = lp;
		y = lp;
	}
	out = (int32_t)sar64((int64_t)(y / 256) * cv, 16);
	pan = v->psst >> 24;
	*l += out * (int32_t)pan / 255;
	*r += out * (int32_t)(255 - pan) / 255;
	*rev += out * (int32_t)(v->ptrx >> 8 & 0xff) / 255;
	*cho += out * (int32_t)(v->csl >> 24) / 255;
}

/* A sample of the chip: a channel's envelope engine, every voice, the effects. */
static void
sample(void)
{
	int32_t l, r, rev, cho, o[2];
	Voice *v;
	int i;

	v = &voice[made % Nchan];
	engine(v);
	v->k = 0;
	l = r = rev = cho = 0;
	for (i = 0; i < Nchan; i++)
		voicesample(&voice[i], &l, &r, &rev, &cho);
	emu8kfx(rev, cho, &l, &r);
	o[0] = o[1] = 0;
	if (hwcf3 & 4) {
		o[0] = l;
		o[1] = r;
	}
	made++;
	if (sink != NULL)
		sink(o, 1);
}

/* The chip's time moved by an access, the samples before it made. */
static void
access(void)
{
	t += Tio;
	while (made < t >> 16)
		sample();
}

/* emu8krun: the samples up to s made, the chip's time brought there. */
void
emu8krun(uint64_t s)
{
	if (t < s << 16)
		t = s << 16;
	while (made < t >> 16)
		sample();
}

/* emu8kmade: the samples made. */
uint64_t
emu8kmade(void)
{
	return made;
}

/*
 * emu8kreset: the chip as at power on, the sound ROM given (NULL for
 * none). The guide has its registers random then. Here they are 0 and
 * every envelope engine off (DCYSUSV 0x80), so that a channel a driver
 * leaves alone, as A32GFX.DLL leaves 30 and 31, is silent, and HWCF1 and
 * HWCF2 are as the system's initialisation of the card leaves them
 * (86Box's), the output muted.
 */
void
emu8kreset(uint16_t *r, void (*out)(int32_t*, int))
{
	int i;

	memset(voice, 0, sizeof voice);
	for (i = 0; i < Nchan; i++) {
		voice[i].dcysusv = 0x80;
		voice[i].vol.phase = voice[i].mod.phase = Erelease;
		voice[i].vol.att = voice[i].mod.att = 0xffff;
	}
	memset(init, 0, sizeof init);
	memset(ram, 0, sizeof ram);
	hwcf1 = 0x59;
	hwcf2 = 0x20;
	hwcf3 = u7 = ptr = smld = smrd = 0;
	t = made = 0;
	rom = r;
	sink = out;
	emu8kfxreset();
}

/* The effects given the words that set them: the INIT arrays, HWCF5. */
static void
fxwords(void)
{
	emu8kfxinit(init, voice[10].r1);
}

/* A DMA stream's channel: one with CCCA's DMA bit, reading or writing, left or right. */
static int
stream(uint32_t wr, uint32_t right)
{
	int i;

	for (i = 0; i < Nchan; i++)
		if ((voice[i].ccca >> 24 & 7) == (4 | wr<<1 | right))
			return 1;
	return 0;
}

/*
 * Register 1 of Data1: the streams, the counter, HWCF1 to 3, else a
 * latch. A read of SMLD or SMRD gives the word fetched by the read
 * before and fetches the next from its address, so that the first after
 * the address is set is stale, as the guide says.
 */
static void
r1write(Voice *v, int ch, int hi, uint32_t val)
{
	switch (ch) {
	case 26:
		if (hi) {
			if (stream(1, 1)) {
				memwrite(voice[23].r1, val);
				voice[23].r1 = (voice[23].r1 + 1) & 0xffffff;
			}
		} else if (stream(1, 0)) {
			memwrite(voice[22].r1, val);
			voice[22].r1 = (voice[22].r1 + 1) & 0xffffff;
		}
		return;
	case 27:
		if (!hi)
			v->r1 = (v->r1 & 0xffff0000) | val;
		return;
	case 29:
		if (!hi)
			hwcf1 = val;
		return;
	case 30:
		if (!hi)
			hwcf2 = val;
		return;
	case 31:
		if (!hi)
			hwcf3 = val;
		return;
	}
	if (hi)
		v->r1 = (v->r1 & 0xffff) | val<<16;
	else
		v->r1 = (v->r1 & 0xffff0000) | val;
}

static uint32_t
r1read(Voice *v, int ch, int hi)
{
	uint32_t w;

	switch (ch) {
	case 26:
		if (hi) {
			w = smrd;
			if (stream(0, 1)) {
				smrd = mem(voice[21].r1);
				voice[21].r1 = (voice[21].r1 + 1) & 0xffffff;
			}
			return w;
		}
		w = smld;
		if (stream(0, 0)) {
			smld = mem(voice[20].r1);
			voice[20].r1 = (voice[20].r1 + 1) & 0xffffff;
		}
		return w;
	case 27:
		return hi ? made & 0xffff : v->r1 & 0xffff;
	case 29:
		if (!hi)
			return (hwcf1 & 0xfe) | (hwcf3 & 1);
		break;
	case 30:
		if (!hi)
			return (hwcf2 >> 4 & 0x0e) | (hwcf1 & 1) | (hwcf3 & 2 ? 0x10 : 0) | (hwcf3 & 4 ? 0x40 : 0)
				| (hwcf3 & 8 ? 0x20 : 0) | (hwcf3 & 0x10 ? 0x80 : 0);
		break;
	case 31:
		if (!hi)
			return hwcf2 & 0x1f;
		break;
	}
	if (ch >= 20 && ch <= 23 && hi)
		return v->r1 >> 16 & 0xff;
	return hi ? v->r1 >> 16 : v->r1 & 0xffff;
}

/* The half of a doubleword register a word sets. */
static void
half(uint32_t *r, int hi, uint32_t val)
{
	if (hi)
		*r = (*r & 0xffff) | val<<16;
	else
		*r = (*r & 0xffff0000) | val;
}

/* emu8kout: a word written to a port, from the base. */
void
emu8kout(uint32_t port, uint32_t val)
{
	Voice *v;
	int reg, ch, hi, on;

	access();
	val &= 0xffff;
	if (port == Ppointer) {
		ptr = val & 0xff;
		return;
	}
	reg = ptr >> 5;
	ch = ptr & 0x1f;
	v = &voice[ch];
	switch (port) {
	case Pdata0:
	case Pdata0h:
		hi = port == Pdata0h;
		switch (reg) {
		case 0: half(&v->cpf, hi, val); break;
		case 1: half(&v->ptrx, hi, val); break;
		case 2: half(&v->cvcf, hi, val); break;
		case 3: half(&v->vtft, hi, val); break;
		case 4: half(&v->z2, hi, val); break;
		case 5: half(&v->z1, hi, val); break;
		case 6: half(&v->psst, hi, val); break;
		case 7: half(&v->csl, hi, val); break;
		}
		return;
	case Pdata1:
		switch (reg) {
		case 0: half(&v->ccca, 0, val); break;
		case 1: r1write(v, ch, 0, val); break;
		case 2: init[0][ch] = val; break;
		case 3: init[2][ch] = val; break;
		case 4: v->envvol = val; break;
		case 5:
			on = (v->dcysusv & 0x80) == 0;
			v->dcysusv = val;
			if (val & 0x80)
				break;
			if (val & 0x8000)
				release(&v->vol);
			else if (!on)
				start(v);
			break;
		case 6: v->envval = val; break;
		case 7:
			v->dcysus = val & 0xff7f;
			if (val & 0x8000)
				release(&v->mod);
			break;
		}
		if (reg == 1 || reg == 2 || reg == 3)
			fxwords();
		return;
	case Pdata2:
		switch (reg) {
		case 0: half(&v->ccca, 1, val); break;
		case 1: r1write(v, ch, 1, val); break;
		case 2: init[1][ch] = val; break;
		case 3: init[3][ch] = val; break;
		case 4: v->atkhldv = val & 0xff7f; break;
		case 5: v->lfo1val = val; v->lfo1 = 0; break;
		case 6: v->atkhld = val & 0xff7f; break;
		case 7: v->lfo2val = val; v->lfo2 = 0; break;
		}
		if (reg == 1 || reg == 2 || reg == 3)
			fxwords();
		return;
	case Pdata3:
		switch (reg) {
		case 0: v->ip = val; break;
		case 1: v->ifatn = val; break;
		case 2: v->pefe = val; break;
		case 3: v->fmmod = val; break;
		case 4: v->tremfrq = val; break;
		case 5: v->fm2frq2 = val; break;
		case 7: u7 = val; break;
		}
		return;
	}
}

/* emu8kin: a word read from a port, from the base. */
uint32_t
emu8kin(uint32_t port)
{
	Voice *v;
	int reg, ch, hi;

	access();
	if (port == Ppointer)
		return (0x80 | (t >> 11 & 0x1f)) << 8 | ptr;
	reg = ptr >> 5;
	ch = ptr & 0x1f;
	v = &voice[ch];
	switch (port) {
	case Pdata0:
	case Pdata0h:
		hi = port == Pdata0h;
		switch (reg) {
		case 0: return hi ? v->cpf >> 16 : v->cpf & 0xffff;
		case 1: return hi ? v->ptrx >> 16 : v->ptrx & 0xffff;
		case 2: return hi ? v->cvcf >> 16 : v->cvcf & 0xffff;
		case 3: return hi ? v->vtft >> 16 : v->vtft & 0xffff;
		case 4: return hi ? v->z2 >> 16 : v->z2 & 0xffff;
		case 5: return hi ? v->z1 >> 16 : v->z1 & 0xffff;
		case 6: return hi ? v->psst >> 16 : v->psst & 0xffff;
		case 7: return hi ? v->csl >> 16 : v->csl & 0xffff;
		}
		break;
	case Pdata1:
		switch (reg) {
		case 0: return v->ccca & 0xffff;
		case 1: return r1read(v, ch, 0);
		case 2: return init[0][ch];
		case 3: return init[2][ch];
		case 4: return v->envvol;
		case 5: return v->dcysusv;
		case 6: return v->envval;
		case 7: return v->dcysus;
		}
		break;
	case Pdata2:
		switch (reg) {
		case 0: return v->ccca >> 16;
		case 1: return r1read(v, ch, 1);
		case 2: return init[1][ch];
		case 3: return init[3][ch];
		case 4: return v->atkhldv;
		case 5: return v->lfo1val;
		case 6: return v->atkhld;
		case 7: return v->lfo2val;
		}
		break;
	case Pdata3:
		switch (reg) {
		case 0: return v->ip;
		case 1: return v->ifatn;
		case 2: return v->pefe;
		case 3: return v->fmmod;
		case 4: return v->tremfrq;
		case 5: return v->fm2frq2;
		case 6: return 0xffff;
		case 7: return 0x1c | (u7 & 2 ? 0xff02 : 0);
		}
		break;
	}
	return 0xffff;
}
