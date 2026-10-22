#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "dat.h"
#include "fns.h"
#include "abi.h"
#include "pc.h"

/*
 * timeoracle: the timer as lib386/lib_sys/timer_a.asm's handler of IRQ 0
 * counts it and lib386/lib_sys/timer.c installs it, against
 * sys/timer.c, the port's, at the interface the game reads: TimerRef,
 * TimerSystem, NbFramePerSecond and the counters beside them.
 *
 *	timeoracle lba0.elf lba0.sym
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map).
 * Its code runs on pc.c's PC. InitTimer puts NewIntPM08 in vector 8
 * through int 21h and sets the PIT's channel 0 through SetTimer, and the
 * handler is called as IRQ 0 calls it, at the end of each of the PIT's
 * counts: it must see one end of interrupt sent to the PIC, its own or
 * the BIOS's when it chains to the BIOS's handler, which the model
 * counts, and keep every register. sys/timer.c's clock is moved by
 * idle, a millisecond at a time, to each of its ticks, the k-th at its
 * k times 20 ms.
 *
 *	ticks	200000 interrupts, the game's writes between at random (a
 *		frame counted in CmptFrame, TimerRef put back as SaveTimer
 *		and RestoreTimer do): after each TimerRef, TimerSystem,
 *		NbFramePerSecond, WaitNbTicks, CmptFrame and Cmpt_18 the
 *		same, and the BIOS's handler chained to as Cmpt_18 carries,
 *		so that it keeps its 18.2 ticks a second
 *	proc	NewProc08, which AIL's timer calls with a MIDI driver, as
 *		ticks does with NewIntPM08, but for Cmpt_18
 *	pit	SetTimer's control word and divisor, for divisors at random,
 *		InitTimer's, 23864 for 50 Hz, and ClearTimer's, which must
 *		be the BIOS's, 0, a count of 65536
 *	vectors	InitTimer puts the handler in vector 8, ClearTimer the one
 *		it found there
 *
 * What the port does otherwise on purpose is let pass, the rate said:
 *
 *	the port's tick every 20 ms of its clock, where the PIT's 1193182
 *	Hz counted 23864 times gives one every 20000.305 us
 *	Cmpt_18, which the port's tick moves always and AIL's callback
 *	leaves as it is: NewIntPM08 alone reads it
 *
 * Each part runs in a process of its own, ended after 600 seconds, its
 * differences reported, the first 20 in full, and the end is
 * "timeoracle: ok", or "timeoracle: N failed" and status 1.
 */

enum
{
	Rounds = 200000,
	Divisor = 23864,	/* timer.c's DIVISOR, for 50 Hz */
	Idles = 21,	/* a tick's idles, at most */
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	inittimer;
	uintptr_t	cleartimer;
	uintptr_t	settimer;
	uintptr_t	newintpm08;
	uintptr_t	newproc08;
	uint32_t	*timerref;
	uint32_t	*timersystem;
	uint16_t	*nbframepersecond;
	uint16_t	*waitnbticks;
	uint16_t	*cmptframe;
	uint16_t	*cmpt18;
} as;

/* sys/timer.c's, which dat.h leaves to the game's headers */
extern volatile uint32_t	TimerSystem;
extern uint16_t	NbFramePerSecond;
extern uint16_t	WaitNbTicks;
extern uint16_t	CmptFrame;
extern uint16_t	Cmpt_18;

/* the PIT's channel 0 */
static int pitmode;	/* the last control word */
static int pitlow;	/* its low byte written, the high to come */
static uint32_t pitcount;	/* the divisor, 0 a count of 65536 */
static int npit;	/* divisors set */

/* sys/video.c's, sys/sdl.c's and the rest, for sys/timer.c */
Vga	vga;

void
sdlpoll(void)
{
}

void
replay(uint64_t n)
{
	USED(n);
}

void
wavetick(uint64_t t)
{
	USED(t);
}

void
cdtick(uint64_t t)
{
	USED(t);
}

void
sdlshow(void)
{
}

void
summed(uint32_t n, uint64_t t)
{
	USED(n);
	USED(t);
}

uint64_t
sdlnanos(void)
{
	return 0;
}

void
sdlsleep(uint64_t t)
{
	USED(t);
}

void
sdllate(void)
{
}

/* A replay's: the clock runs as fast as the oracle calls. */
int
playing(void)
{
	return 1;
}

/* The port's end, sdl.c's: the C library's exit. */
void
portexit(int status)
{
	exit(status);
}

static uint32_t
rnd(void)
{
	static uint32_t x = 0x9e3779b9;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static uint32_t
pitin(uint32_t port, int w)
{
	USED(w);
	fail("the PIT's port %xh read", port);
	return 0;
}

/* The PIT's ports: 43h a control word, 40h channel 0's count, low byte then high. */
static void
pitout(uint32_t port, uint32_t v, int w)
{
	USED(w);
	if (port == 0x43) {
		if ((v & 0xc0) != 0)
			fail("a control word %02xh for channel %d", v, v >> 6);
		pitmode = v;
		pitlow = 0;
		return;
	}
	if (port != 0x40) {
		fail("the PIT's port %xh written, %02xh", port, v);
		return;
	}
	if ((pitmode & 0x30) != 0x30) {
		fail("channel 0's count written in access mode %d", pitmode >> 4 & 3);
		return;
	}
	if (!pitlow) {
		pitcount = v & 0xff;
		pitlow = 1;
		return;
	}
	pitcount |= (v & 0xff) << 8;
	pitlow = 0;
	npit++;
}

/* The BIOS's handler of IRQ 0, chained to: its tick, which the model counts, and its end of interrupt. */
static void
biosirq(void)
{
	neoi++;
}

/* The six counters the same on both sides, Cmpt_18 if cmpt: 1 if they are. */
static int
same(int cmpt)
{
	if (*as.timerref == TimerRef && *as.timersystem == TimerSystem
	&& *as.nbframepersecond == NbFramePerSecond && *as.waitnbticks == WaitNbTicks
	&& *as.cmptframe == CmptFrame && (!cmpt || *as.cmpt18 == Cmpt_18))
		return 1;
	fail("the assembly's TimerRef %u TimerSystem %u NbFramePerSecond %u WaitNbTicks %u"
		" CmptFrame %u Cmpt_18 %u, ours %u %u %u %u %u %u",
		*as.timerref, *as.timersystem, *as.nbframepersecond, *as.waitnbticks,
		*as.cmptframe, *as.cmpt18, TimerRef, TimerSystem, NbFramePerSecond,
		WaitNbTicks, CmptFrame, Cmpt_18);
	return 0;
}

/* The port's clock moved to its next tick: 1 if it came. */
static int
porttick(void)
{
	uint32_t t;
	int i;

	t = TimerSystem;
	for (i = 0; i < Idles && TimerSystem == t; i++)
		idle();
	if (TimerSystem != t + 1) {
		fail("the port's clock ticked %u times in %d idles", TimerSystem - t, i);
		return 0;
	}
	return 1;
}

/* The game's writes between two ticks, on both sides: a frame drawn, TimerRef put back. */
static void
gamewrites(void)
{
	uint32_t r;

	if (rnd() % 3 == 0) {
		r = rnd() % 4;
		*as.cmptframe += r;
		CmptFrame += r;
	}
	if (rnd() % 50 == 0) {
		r = rnd();
		*as.timerref = r;
		TimerRef = r;
	}
}

static void
ticks(void *v)
{
	uint32_t sel, off;
	int i, e, c, carry, nbios;

	USED(v);
	clockinit();
	snprintf(what, sizeof what, "InitTimer");
	acall(as.inittimer, 0);
	pcvector(8, &sel, &off);
	nbios = 0;
	for (i = 0; i < Rounds; i++) {
		gamewrites();
		snprintf(what, sizeof what, "tick %d", i);
		carry = Cmpt_18 + Divisor > 0xffff;
		e = neoi;
		c = chained(8);
		intcall(off);
		intok("NewIntPM08");
		if (neoi != e + 1)
			fail("%d ends of interrupt", neoi - e);
		if (chained(8) - c != carry)
			fail("the BIOS's handler chained to %d times, %d with Cmpt_18 %u", chained(8) - c,
				carry, Cmpt_18);
		nbios += chained(8) - c;
		if (!porttick())
			break;
		same(1);
	}
	printf("ticks: %d ticks, %d to the BIOS, %s\n", Rounds, nbios, nfail == 0 ? "the same" : "differ");
}

static void
proc(void *v)
{
	int i;

	USED(v);
	clockinit();
	for (i = 0; i < Rounds; i++) {
		gamewrites();
		snprintf(what, sizeof what, "tick %d", i);
		acall(as.newproc08, 0);
		if (!porttick())
			break;
		same(0);
	}
	printf("proc: %d ticks, %s\n", Rounds, nfail == 0 ? "the same" : "differ");
}

static void
pit(void *v)
{
	uint32_t d;
	int i;

	USED(v);
	for (i = 0; i < 1000; i++) {
		d = i == 0 ? 0 : i == 1 ? 0xffff : rnd() & 0xffff;
		snprintf(what, sizeof what, "SetTimer(%u)", d);
		npit = 0;
		acall(as.settimer, 1, d);
		if (npit != 1 || pitcount != d || pitmode != 0x36)
			fail("%d divisors, the last %u, control word %02xh", npit, pitcount, pitmode);
	}
	snprintf(what, sizeof what, "InitTimer");
	acall(as.inittimer, 0);
	if (pitcount != Divisor)
		fail("the PIT's divisor %u", pitcount);
	printf("pit: InitTimer's divisor %u, a tick every %.3f us, the port's every 20000 us\n",
		pitcount, pitcount * 88.0 / 105);
	snprintf(what, sizeof what, "ClearTimer");
	acall(as.cleartimer, 0);
	if (pitcount != 0)
		fail("the PIT's divisor %u, where the BIOS's is 0, a count of 65536", pitcount);
	printf("pit: %s\n", nfail == 0 ? "as it should" : "differs");
}

static void
vectors(void *v)
{
	uint32_t sel0, off0, sel, off;

	USED(v);
	pcvector(8, &sel0, &off0);
	snprintf(what, sizeof what, "InitTimer");
	acall(as.inittimer, 0);
	pcvector(8, &sel, &off);
	if (sel != pccs || off != as.newintpm08)
		fail("vector 8 %04x:%08x, NewIntPM08 is %04x:%08x", sel, off, pccs, (uint32_t)as.newintpm08);
	snprintf(what, sizeof what, "ClearTimer");
	acall(as.cleartimer, 0);
	pcvector(8, &sel, &off);
	if (sel != sel0 || off != off0)
		fail("vector 8 %04x:%08x, it was %04x:%08x", sel, off, sel0, off0);
	printf("vectors: %s\n", nfail == 0 ? "as they should" : "differ");
}

int
main(int argc, char **argv)
{
	static struct
	{
		char	*name;
		void	(*fn)(void*);
	} parts[] = {
		{"ticks", ticks},
		{"proc", proc},
		{"pit", pit},
		{"vectors", vectors},
	};
	int i, nbad;

	oracle = "timeoracle";
	if (argc != 3) {
		fprintf(stderr, "usage: timeoracle lba0.elf lba0.sym\n");
		return 2;
	}
	pcload(argv[1], argv[2]);
	as.inittimer = lookup("InitTimer");
	as.cleartimer = lookup("ClearTimer");
	as.settimer = lookup("SetTimer");
	as.newintpm08 = lookup("NewIntPM08");
	as.newproc08 = lookup("NewProc08");
	as.timerref = (uint32_t*)lookup("TimerRef");
	as.timersystem = (uint32_t*)lookup("TimerSystem");
	as.nbframepersecond = (uint16_t*)lookup("NbFramePerSecond");
	as.waitnbticks = (uint16_t*)lookup("WaitNbTicks");
	as.cmptframe = (uint16_t*)lookup("CmptFrame");
	as.cmpt18 = (uint16_t*)lookup("Cmpt_18");
	pcport(0x40, 0x43, pitin, pitout);
	pcbios(8, biosirq);
	nbad = 0;
	for (i = 0; i < (int)nelem(parts); i++)
		nbad += inchild(parts[i].name, 600, parts[i].fn, NULL);
	if (nbad > 0) {
		fprintf(stderr, "timeoracle: %d failed\n", nbad);
		return 1;
	}
	printf("timeoracle: ok\n");
	return 0;
}
