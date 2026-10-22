#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dat.h"
#include "fns.h"
#include "abi.h"
#include "pc.h"

/*
 * memoracle: the game's memory as lib386/lib_sys/malloc.c gives it, over
 * Watcom's heap and DPMI, against sys/mem.c, the port's, at the
 * interface the game calls: Malloc, Free, Mshrink, SmartMalloc,
 * DosMalloc and Malloc(-1).
 *
 *	memoracle lba0.elf lba0.sym
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map).
 * Its code, Watcom's run-time among it, runs on pc.c's PC, DOS memory
 * up to 1 MB. The run-time grows its heap through DPMI's 0501h, as under
 * DOS/4GW, whose start-up sets _Extender to 1, which the oracle sets.
 * DosMalloc takes DOS memory through 0100h, and Malloc(-1) asks 0500h,
 * which answers 16 MB.
 *
 *	game	the game's allocations in their order, from
 *		InitAdelineSystem's DosMallocs to perso.c's Malloc(-1), the
 *		HQR files' blocks shrunk to their sizes, then Free'd
 *	random	100000 calls at random: Malloc of 1 byte to 1 MB, Free,
 *		Mshrink to a size below a block's, SmartMalloc, DosMalloc,
 *		the blocks held below 60 MB, the DOS blocks below 300 KB
 *	cycles	20 times, blocks of up to 4 MB to about 100 MB, freed in
 *		any order, then one of 100 MB, which the port's arena of
 *		128 MB gives when its holes have merged back
 *
 * After each Malloc, SmartMalloc and DosMalloc of game and random a
 * block must be given on both sides or on neither, aligned on 4 in
 * Watcom's heap, its own, and on 16 in the port's, and it is written
 * whole with a pattern of its own. The blocks held must read their
 * patterns back on both sides after game's allocations and its frees,
 * every 1000 calls of random and at its end, random's Mshrink must keep
 * a block where it is, and Malloc(-1) must give the same on both sides
 * after game's allocations and at random's end: 0500h's 16 MB as
 * malloc.c reads it and sys/mem.c's Freemem, whatever the blocks held.
 * Each block of cycles, its last of 100 MB among them, must be given on
 * both sides. What the port does otherwise on purpose is let pass:
 *
 *	a block's address, and its first bytes: the port clears a block,
 *	Watcom's heap gives it as it held it
 *	DosMalloc's handle, DPMI's selector, the port's 1, and its block,
 *	DOS memory below 1 MB, the port's in its arena
 *	a request past DOS memory, which the port's arena takes
 *
 * Each part runs in a process of its own, ended after 600 seconds, its
 * differences reported, the first 20 in full, and the end is
 * "memoracle: ok", or "memoracle: N failed" and status 1.
 */

enum
{
	Rounds = 100000,
	Nblock = 200,
	Maxsize = 1<<20,
	Maxtotal = 60<<20,	/* the blocks held at once, below the port's arena of 128 MB */
	Maxdos = 40,	/* DOS blocks, which the game keeps to its end */
	Dosend = 0x100000,
	Extender = 1,	/* the run-time's _Extender under DOS/4GW */
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	malloc;
	uintptr_t	free;
	uintptr_t	mshrink;
	uintptr_t	smartmalloc;
	uintptr_t	dosmalloc;
	uint8_t	*extender;
} as;

/* sys/mem.c's interface, which lib386/lib_sys/lib_sys.h declares for the game */
void	Free(void*);
void*	Mshrink(void*, uint32_t);
void*	SmartMalloc(int32_t);
void*	DosMalloc(int32_t, uint32_t*);

/* a block given on both sides */
typedef struct Block Block;

struct Block
{
	uint8_t	*a;
	uint8_t	*p;
	uint32_t	n;
	uint8_t	pat;
	int	dos;	/* DosMalloc's or SmartMalloc's, which the game keeps */
};

static Block blocks[Nblock];
static int nblocks;
static uint32_t total;	/* bytes held */
static int ndos;

static uint32_t
rnd(void)
{
	static uint32_t x = 0x6a09e667;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* Both blocks written with b's pattern. */
static void
fill(Block *b)
{
	memset(b->a, b->pat, b->n);
	memset(b->p, b->pat, b->n);
}

/* Every block reading back its pattern on both sides: 1 if they do. */
static int
intact(void)
{
	Block *b;
	uint32_t i;

	for (b = blocks; b < blocks + nblocks; b++)
		for (i = 0; i < b->n; i++)
			if (b->a[i] != b->pat || b->p[i] != b->pat) {
				fail("a block of %u bytes at byte %u: the assembly's %02x, ours %02x, its pattern %02x",
					b->n, i, b->a[i], b->p[i], b->pat);
				return 0;
			}
	return 1;
}

/* A block given on both sides, or on neither, and kept: 1 if both agree. */
static int
keep(uint8_t *a, uint8_t *p, uint32_t n, int dos)
{
	Block *b;

	if ((a == NULL) != (p == NULL)) {
		fail("the assembly's block %p, ours %p", (void*)a, (void*)p);
		return 0;
	}
	if (a == NULL)
		return 1;
	if (((uintptr_t)a & 3) != 0 || ((uintptr_t)p & 15) != 0) {
		fail("the assembly's block at %p, ours at %p, off 4 and 16", (void*)a, (void*)p);
		return 0;
	}
	if (nblocks == Nblock)
		fatal("too many blocks");
	b = &blocks[nblocks++];
	b->a = a;
	b->p = p;
	b->n = n;
	b->pat = rnd();
	b->dos = dos;
	fill(b);
	total += n;
	if (dos) {
		ndos++;
	}
	return 1;
}

static uint8_t*
amalloc(int32_t n)
{
	return (uint8_t*)(uintptr_t)acall(as.malloc, 1, n);
}

/* Malloc(-1), the same on both sides: 0500h's 16 MB as malloc.c reads it, the port's Freemem. */
static void
largest(void)
{
	int32_t a, p;

	snprintf(what, sizeof what, "Malloc(-1)");
	a = acall(as.malloc, 1, -1);
	p = (int32_t)(intptr_t)Malloc(-1);
	if (a != p)
		fail("the assembly's %d, ours %d", a, p);
}

static void
game(void *v)
{
	static int32_t sizes[] = {
		9600, 24576, 45056,	/* the GTL and two drivers' DLLs, DOS memory */
		640*480, 640*480 + 500,	/* Log, Screen */
	};
	static int32_t smart[] = {2048, 25000, 1024, 5000};
	static int32_t hqr[][2] = {{120000, 100000}, {40000, 39000}, {300000, 250000}, {5000, 4096}};
	uint32_t h;
	uint8_t *a, *p;
	int i;

	USED(v);
	*as.extender = Extender;
	for (i = 0; i < 3; i++) {
		snprintf(what, sizeof what, "DosMalloc(%d)", sizes[i]);
		a = (uint8_t*)(uintptr_t)acall(as.dosmalloc, 2, sizes[i], (int32_t)(uintptr_t)&h);
		p = DosMalloc(sizes[i], &h);
		keep(a, p, sizes[i], 1);
	}
	for (i = 3; i < 5; i++) {
		snprintf(what, sizeof what, "Malloc(%d)", sizes[i]);
		keep(amalloc(sizes[i]), Malloc(sizes[i]), sizes[i], 0);
	}
	snprintf(what, sizeof what, "BufSpeak, DosMalloc(%d)", 256*1024 + 34);
	a = (uint8_t*)(uintptr_t)acall(as.dosmalloc, 2, 256*1024 + 34, 0);
	p = DosMalloc(256*1024 + 34, NULL);
	keep(a, p, 256*1024 + 34, 1);
	for (i = 0; i < 4; i++) {
		snprintf(what, sizeof what, "SmartMalloc(%d)", smart[i]);
		a = (uint8_t*)(uintptr_t)acall(as.smartmalloc, 1, smart[i]);
		p = SmartMalloc(smart[i]);
		keep(a, p, smart[i], 1);
	}
	snprintf(what, sizeof what, "HQM's Malloc(400000)");
	keep(amalloc(400000), Malloc(400000), 400000, 0);
	for (i = 0; i < 4; i++) {
		snprintf(what, sizeof what, "LoadMalloc_HQR's Malloc(%d), Mshrink to %d", hqr[i][0], hqr[i][1]);
		a = amalloc(hqr[i][0]);
		p = Malloc(hqr[i][0]);
		if (!keep(a, p, hqr[i][0], 0) || a == NULL)
			continue;
		acall(as.mshrink, 2, (int32_t)(uintptr_t)a, hqr[i][1]);
		Mshrink(p, hqr[i][1]);
		blocks[nblocks-1].n = hqr[i][1];
	}
	intact();
	largest();
	for (i = nblocks - 1; i >= 0; i--)
		if (!blocks[i].dos) {
			snprintf(what, sizeof what, "Free of %u bytes", blocks[i].n);
			acall(as.free, 1, (int32_t)(uintptr_t)blocks[i].a);
			Free(blocks[i].p);
			blocks[i] = blocks[--nblocks];
		}
	intact();
	printf("game: %s\n", nfail == 0 ? "the same" : "differ");
}

static void
atrandom(void *v)
{
	uint32_t n, h;
	uint8_t *a, *p;
	Block *b;
	int i, k;

	USED(v);
	*as.extender = Extender;
	for (i = 0; i < Rounds && nfail == 0; i++) {
		if (i % 1000 == 0)
			intact();
		k = rnd() % 10;
		if (nblocks > 0 && (k < 3 || nblocks == Nblock)) {
			b = &blocks[rnd() % nblocks];
			if (b->dos)
				continue;
			snprintf(what, sizeof what, "call %d, Free of %u bytes", i, b->n);
			acall(as.free, 1, (int32_t)(uintptr_t)b->a);
			Free(b->p);
			total -= b->n;
			*b = blocks[--nblocks];
		} else if (nblocks > 0 && k < 5) {
			b = &blocks[rnd() % nblocks];
			if (b->dos || b->n < 2)
				continue;
			n = 1 + rnd() % (b->n - 1);
			snprintf(what, sizeof what, "call %d, Mshrink from %u to %u", i, b->n, n);
			a = (uint8_t*)(uintptr_t)acall(as.mshrink, 2, (int32_t)(uintptr_t)b->a, n);
			p = Mshrink(b->p, n);
			if (a != b->a || p != b->p)
				fail("the assembly's block moved to %p, ours to %p", (void*)a, (void*)p);
			total -= b->n - n;
			b->n = n;
		} else if (k < 9) {
			n = 1 + (rnd() % 4 == 0 ? rnd() % Maxsize : rnd() % 4096);
			if (total + n > Maxtotal)
				continue;
			snprintf(what, sizeof what, "call %d, Malloc(%u)", i, n);
			keep(amalloc(n), Malloc(n), n, 0);
		} else if (ndos >= Maxdos) {
			continue;
		} else if (rnd() % 2 == 0) {
			n = 1 + rnd() % 4096;
			snprintf(what, sizeof what, "call %d, SmartMalloc(%u)", i, n);
			keep((uint8_t*)(uintptr_t)acall(as.smartmalloc, 1, n), SmartMalloc(n), n, 1);
		} else {
			n = 1 + rnd() % 2048;
			snprintf(what, sizeof what, "call %d, DosMalloc(%u)", i, n);
			a = (uint8_t*)(uintptr_t)acall(as.dosmalloc, 2, n, (int32_t)(uintptr_t)&h);
			p = DosMalloc(n, &h);
			keep(a, p, n, 1);
		}
	}
	intact();
	largest();
	printf("random: %d calls, %d blocks at the end, %s\n", i, nblocks, nfail == 0 ? "the same" : "differ");
}

static void
cycles(void *v)
{
	uint8_t *a[200], *p[200], *ba, *bp;
	uint32_t n[200], sum;
	int c, i, j, k;

	USED(v);
	*as.extender = Extender;
	for (c = 0; c < 20 && nfail == 0; c++) {
		sum = 0;
		for (k = 0; k < 200 && sum < 96<<20; k++) {
			n[k] = 1 + rnd() % (4<<20);
			snprintf(what, sizeof what, "cycle %d, Malloc(%u) with %u held", c, n[k], sum);
			a[k] = amalloc(n[k]);
			p[k] = Malloc(n[k]);
			if (a[k] == NULL || p[k] == NULL) {
				fail("the assembly's block %p, ours %p", (void*)a[k], (void*)p[k]);
				return;
			}
			sum += n[k];
		}
		for (i = k - 1; i > 0; i--) {
			j = rnd() % (i + 1);
			ba = a[i];
			a[i] = a[j];
			a[j] = ba;
			bp = p[i];
			p[i] = p[j];
			p[j] = bp;
		}
		for (i = 0; i < k; i++) {
			acall(as.free, 1, (int32_t)(uintptr_t)a[i]);
			Free(p[i]);
		}
		snprintf(what, sizeof what, "cycle %d, Malloc(%u) after %d blocks freed", c, 100<<20, k);
		ba = amalloc(100<<20);
		bp = Malloc(100<<20);
		if (ba == NULL || bp == NULL) {
			fail("the assembly's block %p, ours %p", (void*)ba, (void*)bp);
			return;
		}
		acall(as.free, 1, (int32_t)(uintptr_t)ba);
		Free(bp);
	}
	printf("cycles: %d cycles of about 100 MB, %s\n", c, nfail == 0 ? "the same" : "differ");
}

int
main(int argc, char **argv)
{
	int nbad;

	oracle = "memoracle";
	if (argc != 3) {
		fprintf(stderr, "usage: memoracle lba0.elf lba0.sym\n");
		return 2;
	}
	lowend = Dosend;
	pcload(argv[1], argv[2]);
	as.malloc = lookup("Malloc");
	as.free = lookup("Free");
	as.mshrink = lookup("Mshrink");
	as.smartmalloc = lookup("SmartMalloc");
	as.dosmalloc = lookup("DosMalloc");
	as.extender = (uint8_t*)lookup("_Extender");
	nbad = inchild("game", 600, game, NULL);
	nbad += inchild("random", 600, atrandom, NULL);
	nbad += inchild("cycles", 600, cycles, NULL);
	if (nbad > 0) {
		fprintf(stderr, "memoracle: %d failed\n", nbad);
		return 1;
	}
	printf("memoracle: ok\n");
	return 0;
}
