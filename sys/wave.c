#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The samples, as lib386/lib_samp/wave_a.asm mixed them for a Sound
 * Blaster 16 (W_SB16.DLL, 16-bit stereo): the procedures the game calls
 * (WavePlay and its kin, wave_i.asm's names for them) and the mixing its
 * interrupt did into each half of the card's buffer, here
 * done as the port's clock passes each half (wavetick).
 *
 * A sample is a VOC, its first block 8-bit unsigned PCM, played at its
 * rate bent by the pitch (4096 none) as a step of 16 bits of fraction
 * per frame of PlayRate, for as many frames as cover it, repeated, then
 * followed by its son if one was plugged after it. A voice is a struct
 * in CurrentList, a son waits in SonList, each list ended by an empty
 * address. Mixing, the first voice of the list writes the half and the
 * others add to it, each frame a word of 32 bits, left in the low half
 * and right in the high, added whole so that the left's carry goes into
 * the right. A sample takes its value as is, or between it and the one
 * before by the fraction when the buffer's first byte, the VOC's, is
 * below 9 (the game's voices put 0 or 1 there, which says whether more
 * follows). Volumes are kept a quarter of what the game gives, so that
 * at 1024 a byte spans the 16 bits. The game gives its samples 128 and
 * its voices 512, leaving room for several at once. Pausing fades the
 * half out, then is silent. Continuing fades it in.
 */

enum
{
	Half = 1024,	/* BUFFER_SIZE: frames a half-buffer */
	Nlist = 50,	/* LIST_SIZE */
	Entry = 44,	/* STRUCT_SIZE: a voice's bytes in the driver's lists */
	Volshift = 3 - 1,	/* SHIFT_SAMPLE-1 */
};

typedef struct Voice Voice;

struct Voice
{
	uint8_t	*addr;	/* the next byte (NULL ends a list) */
	uint16_t	fract;
	uint32_t	left;	/* frames */
	uint16_t	repeat;
	uint16_t	incr;
	int32_t	son;	/* the long handle that follows, -1 */
	uint32_t	handle;	/* the long handle */
	uint8_t	*start;
	uint32_t	length;
	uint16_t	volleft;
	uint16_t	volright;
	uint32_t	info0;
	uint8_t	interpol;
	uint8_t	last;	/* the byte before addr's, for interpolating */
	uint16_t	asked[4];	/* FEATURE: the repeat, pitch and volumes WavePlay was given, for waveloop. The port's own. */
};

typedef struct Snap Snap;

/* an entry of WaveGetSnap's list, the game's T_WAVE */
struct Snap
{
	uint32_t	handle;	/* the long handle */
	uint32_t	info0;
	uint32_t	played;	/* bytes from the sample's start */
};

int32_t	Wave_Driver_Enable;
int32_t	PlayRate = 22000;

static Voice current[Nlist+1], sons[Nlist+1];
static Voice backcurrent[Nlist+1], backsons[Nlist+1];	/* WaveSaveState's */
static Snap snaps[2*Nlist];
static uint8_t flagpause;
static int32_t follow = -1, backfollow = -1;
static uint32_t weirdcount = 0x10000, backweirdcount;
static uint32_t buffer[Half];
static uint64_t mixed;	/* halves */

static void
removevoice(Voice *v)
{
	for (; v[1].addr != NULL; v++)
		v[0] = v[1];
	v->addr = NULL;
}

static Voice*
searchlong(uint32_t h)
{
	Voice *v;

	/*
	 * FIXED: W_SB16.DLL's SearchLongHandle tests where a list ends
	 * against where SonList starts, which differ while a sample waits in
	 * SonList, so that a long handle in neither list is looked for there
	 * for ever. This looks in each list once.
	 */
	for (v = current; v->addr != NULL; v++)
		if (v->handle == h)
			return v;
	for (v = sons; v->addr != NULL; v++)
		if (v->handle == h)
			return v;
	return NULL;
}

/*
 * n frames of a voice from v->addr at fract into buf, written or added.
 * Interpolating, the byte before is last, and the voice keeps it at
 * the end, though after a carry the byte it would be is the one read.
 */
static void
mixvoice(Voice *v, uint32_t *buf, uint32_t n, int add)
{
	uint8_t *p, prev;
	uint32_t out, sum;
	int32_t s, vl, vr;
	uint16_t fr;
	int carry;

	p = v->addr;
	fr = v->fract;
	carry = 0;
	if (v->interpol) {
		vl = (int16_t)v->volleft;
		vr = (int16_t)v->volright;
		prev = v->last;
		goto next;
	start:
		prev = *p++;
	next:
		s = (int8_t)(prev ^ 0x80) * (65536 - (int32_t)fr) + (int8_t)(*p ^ 0x80) * (int32_t)fr;
		out = ((uint32_t)s*(uint32_t)vr & 0xFFFF0000) | (uint16_t)((int32_t)((uint32_t)s*(uint32_t)vl) >> 16);
		*buf = add ? *buf + out : out;
		buf++;
		sum = (uint32_t)fr + v->incr;
		carry = sum >> 16;
		fr = sum;
		if (--n != 0) {
			if (!carry)
				goto next;
			goto start;
		}
		v->last = prev;
		p++;
	} else {
		vl = (int32_t)((uint32_t)v->volright<<16 | v->volleft);
		do {
			s = (int8_t)(*p++ ^ 0x80);
			out = ((uint32_t)s*(uint32_t)vl & 0xFFFF0000) | (uint16_t)(s*(int16_t)v->volleft);
			do {
				*buf = add ? *buf + out : out;
				buf++;
				sum = (uint32_t)fr + v->incr;
				carry = sum >> 16;
				fr = sum;
			} while (--n != 0 && !carry);
		} while (n != 0);
	}
	if (!carry)
		p--;
	v->addr = p;
	v->fract = fr;
}

/*
 * UpdateBuffer, wave_a.asm's: the next half mixed from CurrentList into
 * buffer (a voice that ends repeated, or followed by its son, or gone),
 * then faded as a pause asks.
 */
static void
mixhalf(void)
{
	Voice *v, *s;
	uint32_t *buf, room, n, i, k, *q;
	int add;

	if (flagpause == 1 || current[0].addr == NULL) {
		memset(buffer, 0, sizeof buffer);
		flagpause &= 1;
		return;
	}
	v = current;
	add = 0;
	while (v->addr != NULL) {
		buf = buffer;
		room = Half;
	fill:
		if (room == 0) {
			v++;
			add = 1;
			continue;
		}
		if (v->left > 0xFFFF || v->left > room) {
			n = room;
			v->left -= n;
			mixvoice(v, buf, n, add);
			v++;
			add = 1;
			continue;
		}
		n = v->left;
		mixvoice(v, buf, n, add);
		buf += n;
		room -= n;
		if (--v->repeat != 0) {
			v->addr = v->start;
			v->left = v->length;
			v->fract = 0;
			goto fill;
		}
		if (v->son >= 0)
			for (s = sons; s->addr != NULL; s++)
				if (s->handle == (uint32_t)v->son) {
					*v = *s;
					removevoice(s);
					v->addr = v->start;
					goto fill;
				}
		if (!add)
			memset(buf, 0, room*sizeof buf[0]);
		removevoice(v);
		add = 1;
	}
	if (flagpause < 2)
		return;
	/* in: frame i by i+1 over 1024, the last left, and out: frame i by 1024-i, the first left */
	q = buffer;
	for (i = 0; i < Half; i++, q++) {
		k = flagpause == 2 ? i+1 : Half-i;
		*q = (uint32_t)(((int32_t)(int16_t)(*q>>16) * (int32_t)k) >> 10) << 16
			| (uint16_t)(((int32_t)(int16_t)*q * (int32_t)k) >> 10);
	}
	flagpause = flagpause == 2 ? 0 : 1;
}

/*
 * wavetick: the halves the clock has passed, mixed and handed to the
 * sound card (sdl.c), each its end's time at PlayRate. At ns 0 none.
 */
void
wavetick(uint64_t ns)
{
	uint64_t half;

	if (!Wave_Driver_Enable)
		return;
	half = (uint64_t)Half * 1000000000 / (uint64_t)PlayRate;
	while ((mixed+1)*half <= ns) {
		mixhalf();
		mixed++;
		sdlwave(buffer, Half, PlayRate);
		summedwave(mixed, buffer, Half);
	}
}

/*
 * WavePlay is wave_a.asm's MixSample: the VOC at buf played, its rate
 * bent by pitch over 4096, repeat times, after the sample last played if
 * plug. Its long handle (the call's count above handle), 0 if it
 * cannot be. The frames that cover it are reckoned as the assembly did,
 * a guess then a step at a time. The long handle takes the high bits of
 * what the guess left unplayed.
 */
uint32_t
WavePlay(uint16_t handle, uint16_t pitch, uint16_t repeat, uint8_t plug, uint16_t volleft, uint16_t volright, void *buf)
{
	uint8_t *b, *data, interpol;
	uint32_t len, freq, eff, incr, nout, used, rest, h, frac;
	uint64_t dividend;
	Voice *v, *list;

	if (!Wave_Driver_Enable)
		return 0;
	b = buf;
	interpol = b[0] + 1;
	if (interpol >= 10)
		interpol = 0;
	b += b[0x14] | b[0x15]<<8;
	if (b[0] != 1 || b[5] != 0)
		return 0;
	/*
	 * FIXED: W_SB16.DLL takes 256 less the time constant in a byte, 0 for
	 * a constant of 0, and divides 1000000 by it in 16 bits, which faults
	 * on a rate past 65535 Hz, a constant of 241 or more. This divides in
	 * 32 bits. Taken from LbaWin 0.8.1.
	 */
	freq = 1000000 / (256 - b[4]);
	len = ((uint32_t)b[1] | b[2]<<8 | b[3]<<16) - 2;
	data = b + 6;
	eff = freq * ((uint32_t)pitch << 4);
	eff = (uint16_t)((eff >> 16) + (eff >> 15 & 1));
	/*
	 * FIXED: W_SB16.DLL divides in 16 bits, which faults on a step past
	 * 16 bits before its test brings the step down to 0xFFFF, and divides
	 * the frames by the step, which faults on a step of 0. This divides in
	 * 32 bits, brings the step down as the test meant, so that the sample
	 * plays at about PlayRate, and plays no sample of step 0.
	 */
	incr = (eff << 16) / (uint16_t)PlayRate;
	if (incr > 0xFFFF)
		incr = 0xFFFF;
	if (incr == 0)
		return 0;
	dividend = (uint64_t)(len >> 16) << 32 | (uint32_t)((len - 1) << 16);
	/*
	 * FIXED: W_SB16.DLL's div faults on a count past 32 bits. This divides
	 * in 64 bits and keeps the low 32.
	 */
	nout = (uint32_t)(dividend / incr) - (len >> 16);
	frac = (uint64_t)nout * incr;
	used = (uint32_t)(((uint64_t)nout * incr) >> 16);
	if (len <= used)
		return 0;
	rest = len - used;
	frac &= 0xFFFF;
	h = rest & 0xFFFF0000;
	rest &= 0xFFFF;
	do {
		do {
			nout++;
			frac += incr;
		} while (frac < 0x10000);
		frac &= 0xFFFF;
	} while (--rest & 0xFFFF);
	h |= handle | weirdcount;
	weirdcount += 0x10000;
	list = current;
	if (plug) {
		v = searchlong(follow);
		/*
		 * FIXED: W_SB16.DLL returns here with a push of its own on the
		 * stack, so its epilogue restores EBX, EDI and ESI a slot off.
		 * C keeps the caller's registers.
		 */
		if (v == NULL)
			return 0;
		v->son = h;
		list = sons;
	}
	for (v = list; v->addr != NULL; v++)
		;
	/*
	 * FIXED: the CD's drivers leave a full list by typeunknown, their
	 * critical section entered, so that the card's interrupt mixes no
	 * half until a call leaves one. Nothing is held here.
	 */
	if (v >= list + Nlist)
		return 0;
	follow = h;
	memset(v, 0, sizeof *v);
	v->handle = h;
	v->repeat = repeat;
	v->left = nout;
	v->length = nout;
	v->addr = data;
	v->start = data;
	v->incr = incr;
	v->fract = 0;
	v->son = -1;
	v->info0 = -1;
	v->interpol = interpol;
	v->last = 0x80;
	v->volleft = volleft >> Volshift;
	v->volright = volright >> Volshift;
	v->asked[0] = repeat;
	v->asked[1] = pitch;
	v->asked[2] = volleft;
	v->asked[3] = volright;
	v[1].addr = NULL;
	return follow;
}

/*
 * FEATURE: waveloop, for the saves of the running scene
 * (sources/savegame.c): the nth sample playing for ever (asked to
 * repeat 0 times) as WavePlay was given it, and its info, or 0 past the
 * last. The port's own.
 */
int32_t
waveloop(int32_t n, uint16_t *handle, uint16_t *pitch, uint16_t *volleft, uint16_t *volright, uint32_t *info0)
{
	Voice *v;

	if (!Wave_Driver_Enable)
		return 0;
	for (v = current; v->addr != NULL; v++)
		if (v->asked[0] == 0 && n-- == 0) {
			*handle = (uint16_t)v->handle;
			*pitch = v->asked[1];
			*volleft = v->asked[2];
			*volright = v->asked[3];
			*info0 = v->info0;
			return 1;
		}
	return 0;
}

/* n bytes of v, the lowest first. */
static uint8_t*
putle(uint8_t *p, uint32_t v, int n)
{
	while (n-- > 0) {
		*p++ = v;
		v >>= 8;
	}
	return p;
}

/*
 * wavestate: the two lists, the pause, follow and weirdcount into p as
 * W_SB16.DLL lays out CurrentList, SonList, FlagPause, follow and
 * weirdcount, 4417 bytes, for the oracle, which compares them with the
 * driver's. A list is its voices of 44 bytes, then an address of 0. An
 * address is the C's, in 32 bits. What lies past a list's end is left.
 */
void
wavestate(uint8_t *p)
{
	Voice *v, *list;
	uint8_t *e;
	int pass;

	for (pass = 0; pass < 2; pass++) {
		list = pass == 0 ? current : sons;
		e = p;
		for (v = list; v->addr != NULL; v++) {
			e = putle(e, (uint32_t)(uintptr_t)v->addr, 4);
			e = putle(e, 0x10000 | v->fract, 4);
			e = putle(e, v->left, 4);
			e = putle(e, v->repeat, 2);
			e = putle(e, v->incr, 2);
			e = putle(e, v->son, 4);
			e = putle(e, v->handle, 4);
			e = putle(e, (uint32_t)(uintptr_t)v->start, 4);
			e = putle(e, v->length, 4);
			e = putle(e, v->volleft, 2);
			e = putle(e, v->volright, 2);
			e = putle(e, v->info0, 4);
			e = putle(e, v->interpol, 1);
			e = putle(e, v->last, 1);
			e = putle(e, 0, 2);
		}
		putle(e, 0, 4);
		p += Entry*Nlist + 4;
	}
	p = putle(p, flagpause, 1);
	p = putle(p, follow, 4);
	putle(p, weirdcount, 4);
}

/* WaveGiveInfo0 is wave_a.asm's GiveSampleInfo0: what the game keeps with a sample. */
void
WaveGiveInfo0(uint32_t longhandle, uint32_t info)
{
	Voice *v;

	if (!Wave_Driver_Enable)
		return;
	v = searchlong(longhandle);
	if (v != NULL)
		v->info0 = info;
}

/* WaveStop is wave_a.asm's StopSample: every sample, and the pause, gone. */
void
WaveStop(void)
{
	if (!Wave_Driver_Enable)
		return;
	current[0].addr = NULL;
	sons[0].addr = NULL;
	flagpause = 0;
	follow = -1;
	weirdcount = 0x10000;
}

/*
 * WaveStopOne is wave_a.asm's StopOneSample: every sample of the
 * handle, in either list, and the samples plugged after them, which
 * goes for every waiting sample of their handles.
 */
void
WaveStopOne(uint16_t handle)
{
	uint16_t stack[2*Nlist+1];
	Voice *v, *list;
	int sp, pass;

	if (!Wave_Driver_Enable)
		return;
	sp = 0;
	for (pass = 0;; pass = 1) {
		list = pass == 0 ? current : sons;
		for (v = list; v->addr != NULL;) {
			if ((uint16_t)v->handle != handle) {
				v++;
				continue;
			}
			if (v->son >= 0 && sp < (int)nelem(stack))
				stack[sp++] = v->son;
			removevoice(v);
		}
		if (pass == 0)
			continue;
		if (sp == 0)
			return;
		handle = stack[--sp];
	}
}

/*
 * WaveStopOneLong is wave_a.asm's StopOneSampleLong: the sample of the
 * long handle stopped, playing or waiting. A sample plugged after it
 * waits on.
 */
void
WaveStopOneLong(uint32_t longhandle)
{
	Voice *v;

	if (!Wave_Driver_Enable)
		return;
	v = searchlong(longhandle);
	if (v != NULL)
		removevoice(v);
}

/* WaveInList is wave_a.asm's SampleInList: whether a sample of the handle plays or waits. */
int
WaveInList(uint16_t handle)
{
	Voice *v;

	if (!Wave_Driver_Enable)
		return 0;
	for (v = current; v->addr != NULL; v++)
		if ((uint16_t)v->handle == handle)
			return 1;
	for (v = sons; v->addr != NULL; v++)
		if ((uint16_t)v->handle == handle)
			return 1;
	return 0;
}

/*
 * WaveGetSnap is wave_a.asm's GetSnapSample: the long handle, info and
 * bytes played of each sample, playing then waiting, in a list *buf
 * points to, and how many.
 */
int
WaveGetSnap(void **buf)
{
	Voice *v, *list;
	Snap *s;
	int pass;

	if (!Wave_Driver_Enable)
		return 0;
	*buf = snaps;
	/*
	 * FIXED: W_SB16.DLL's GetSnapSample tests where a list ends against
	 * where SonList starts, so that while a sample waits in SonList it
	 * walks it again and again, writing past its list, which holds 33
	 * entries where both lists may hold 100, and it counts the entries as
	 * the bytes over 8, where an entry has 12. This walks each list once,
	 * into room for both, and counts the entries.
	 */
	s = snaps;
	for (pass = 0; pass < 2; pass++) {
		list = pass == 0 ? current : sons;
		for (v = list; v->addr != NULL; v++, s++) {
			s->handle = v->handle;
			s->info0 = v->info0;
			s->played = (uint32_t)(v->addr - v->start);
		}
	}
	return (int)(s - snaps);
}

/*
 * WavePause is wave_a.asm's PauseSample: a fade out asked, and whether
 * done. The game waits for it, and here the mixing goes on with the
 * clock: a pause not yet done gives it time.
 */
int
WavePause(void)
{
	int done;

	if (!Wave_Driver_Enable)
		return 0;
	flagpause = ((flagpause - 1) & 2) | 1;
	done = (flagpause >> 1) ^ 1;
	if (!done)
		idle();
	return done;
}

/* WaveContinue is wave_a.asm's ContinueSample: a fade in asked. */
void
WaveContinue(void)
{
	if (!Wave_Driver_Enable)
		return;
	flagpause = (flagpause + 1) & 2;
}

/* CopyList, wave_a.asm's: the voices of s into d up to the empty address, and the end. */
static void
copylist(Voice *d, Voice *s)
{
	for (; s->addr != NULL; s++, d++)
		*d = *s;
	d->addr = NULL;
}

/*
 * WaveSaveState is wave_a.asm's SaveStateSample: once a pause has faded
 * the half out, both lists, follow and weirdcount kept, then the lists
 * emptied and the pause ended for other samples. The clock mixes the half
 * that fades out as the card's interrupt did, each idle pumping.
 */
void
WaveSaveState(void)
{
	if (!Wave_Driver_Enable)
		return;
	WavePause();
	while (flagpause != 1)
		idle();
	copylist(backcurrent, current);
	copylist(backsons, sons);
	backfollow = follow;
	backweirdcount = weirdcount;
	current[0].addr = NULL;
	sons[0].addr = NULL;
	flagpause = 0;
}

/*
 * WaveRestoreState is wave_a.asm's RestoreStateSample: once a pause has
 * faded the half out, the lists, follow and weirdcount WaveSaveState kept
 * put back, then faded in.
 */
void
WaveRestoreState(void)
{
	if (!Wave_Driver_Enable)
		return;
	WavePause();
	while (flagpause != 1)
		idle();
	/*
	 * FIXED: W_SB16.DLL's RestoreStateSample loads BackSonList into EDI
	 * where ESI was meant, so that SonList is copied back from where the
	 * copy of BackCurrentList ended and comes back empty, and loads
	 * weirdcount where it means to store it. This puts both back.
	 */
	copylist(current, backcurrent);
	copylist(sons, backsons);
	follow = backfollow;
	weirdcount = backweirdcount;
	WaveContinue();
	while (flagpause != 0)
		idle();
}

/*
 * WaveChangeVolume is wave_a.asm's ChangeVolume: a sample's volumes,
 * playing or waiting, kept a quarter of what the game gives, as WavePlay
 * keeps them.
 */
void
WaveChangeVolume(uint32_t longhandle, uint32_t volleft, uint32_t volright)
{
	Voice *v;

	if (!Wave_Driver_Enable)
		return;
	v = searchlong(longhandle);
	if (v == NULL)
		return;
	v->volleft = volleft >> Volshift;
	v->volright = volright >> Volshift;
}

/*
 * WaveMove is wave_a.asm's ShiftSamples: the bytes moved forward, as
 * rep movsd then rep movsb move them, a doubleword at a time then the
 * last bytes, and each sample from src on moved with them. The HQR
 * cache moves them down as it packs itself.
 */
void
WaveMove(void *dst, void *src, uint32_t n)
{
	uint8_t *d, *s, w[4];
	ptrdiff_t delta;
	Voice *v;
	uint32_t i;
	int pass;

	d = dst;
	s = src;
	for (i = 0; i < n/4; i++, d += 4, s += 4) {
		memmove(w, s, 4);
		memmove(d, w, 4);
	}
	for (i = 0; i < n%4; i++)
		*d++ = *s++;
	if (!Wave_Driver_Enable)
		return;
	delta = (uint8_t*)dst - (uint8_t*)src;
	for (pass = 0; pass < 2; pass++)
		for (v = pass == 0 ? current : sons; v->addr != NULL; v++)
			if ((uint8_t*)src <= v->start) {
				v->start += delta;
				v->addr += delta;
			}
}
