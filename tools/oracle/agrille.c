#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asmoracle.h"

/*
 * grille_a's routines: a random scene of blocks, most cells empty, a
 * random table of blocks, positions in, on the edges of and outside the
 * map, and random packed columns. Ours and the assembly's results are
 * compared whole, with the map coordinates they leave.
 */

enum
{
	Ncube = 64 * 64 * 25 * 2,
	Nblock = 40,
	Ncol = 64 * 256 * 2,
};

int32_t	WorldColBrickFull(int32_t, int32_t, int32_t, int32_t);
uint8_t	WorldColBrick(int16_t, int16_t, int16_t);
int32_t	WorldCodeBrick(int32_t, int32_t, int32_t);
void	*GetAdrBlock(int32_t);
void	Map2Screen(int32_t, int32_t, int32_t);
void	DecompColonne(void*, void*);
void	MixteColonne(void*, void*);

/* ours, which grille.c defines in the game */
uint8_t	*BufCube, *TabBlock;
int32_t	XMap, YMap, ZMap, XScreen, YScreen;

/* the screen's size, from which Map2Screen's C takes the scene's centre */
extern int32_t	Screen_X, Screen_Y;

static uint8_t cube[Ncube + 65536], blocks[Nblock*(3+4*32) + 4*Nblock];
static int nbricks[Nblock];

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
fill(uint8_t *p, uint32_t n)
{
	uint32_t i;

	for (i = 0; i < n; i++)
		p[i] = rnd();
}

/* The scene and the blocks, which both sides only read. */
static void
scene(void)
{
	uint32_t off;
	int i, b;

	off = 4 * Nblock;
	for (i = 0; i < Nblock; i++) {
		put32(blocks + 4*i, off);
		nbricks[i] = rndr(1, 32);
		fill(blocks + off, 3 + 4*nbricks[i]);
		off += 3 + 4*nbricks[i];
	}
	fill(cube, sizeof cube);
	/*
	 * past the map's top corner a position reads on past the cube, and a
	 * ZMap of 64 would read there: cells as the map's, to the end
	 */
	for (i = 0; i < (int)sizeof cube; i += 2) {
		b = rndn(3) == 0 ? rndr(1, Nblock) : 0;
		cube[i] = b;
		if (b != 0)
			cube[i+1] = rndn(nbricks[b-1]);
	}
}

/* A packed column of about n cells. */
static int
column(uint8_t *p, int n)
{
	uint8_t *nb;
	int k, op, len;

	nb = p++;
	*nb = 0;
	len = 1;
	do {
		k = rndr(1, n < 64 ? n : 64);
		op = rndn(3);
		*p++ = op<<6 | (k - 1);
		len++;
		if (op == 1) {
			fill(p, 2*k);
			p += 2 * k;
			len += 2 * k;
		} else if (op == 2) {
			fill(p, 2);
			p += 2;
			len += 2;
		}
		n -= k;
		(*nb)++;
	} while (n > 0 && *nb < 255);
	return len;
}

static char *mapnames[] = {"XMap", "YMap", "ZMap"};
static uint8_t *amap[3];
static int32_t *cmap[3] = {&XMap, &YMap, &ZMap};

/* Whether the assembly's map coordinates are ours. */
static int
samemap(char *what, char *args)
{
	int k;

	for (k = 0; k < 3; k++)
		if ((int32_t)get32(amap[k]) != *cmap[k]) {
			fail("%s %s: %s, ours %d, the assembly's %d", what, args, mapnames[k], *cmap[k],
				(int32_t)get32(amap[k]));
			return 0;
		}
	return 1;
}

void
testgrille(void)
{
	static uint8_t col[Ncol], ad[Ncol], cd[Ncol];
	uintptr_t full, wcb, code, adr, m2s, dec, mix;
	int32_t x, y, z, m, ra, rc;
	uint8_t *p;
	char args[128];
	int r, k;

	full = lookup("WorldColBrickFull");
	wcb = lookup("WorldColBrick");
	code = lookup("WorldCodeBrick");
	adr = lookup("GetAdrBlock");
	m2s = lookup("Map2Screen");
	dec = lookup("DecompColonne");
	mix = lookup("MixteColonne");
	for (k = 0; k < 3; k++)
		amap[k] = (uint8_t*)lookup(mapnames[k]);
	Screen_X = 640;
	Screen_Y = 480;
	scene();
	BufCube = cube;
	TabBlock = blocks;
	put32((uint8_t*)lookup("BufCube"), (uint32_t)(uintptr_t)cube);
	put32((uint8_t*)lookup("TabBlock"), (uint32_t)(uintptr_t)blocks);
	for (r = 0; r < Rounds; r++) {
		if (r % 1000 == 999)
			scene();
		x = rndr(-3000, 64*512+3000);
		y = rndr(-2000, 25*256+2000);
		z = rndr(-3000, 64*512+3000);
		m = rndr(-300, 3000);
		/* a height in four by a multiple of 256, where the cells it covers change */
		if (rndn(4) == 0)
			m = 256*rndn(12) + rndr(-1, 1);
		snprintf(args, sizeof args, "%d %d %d %d", x, y, z, m);
		ra = acall(full, 4, x, y, z, m);
		rc = WorldColBrickFull(x, y, z, m);
		if (ra != rc) {
			fail("WorldColBrickFull %s: ours %#x, the assembly's %#x", args, rc, ra);
			break;
		}
		if (!samemap("WorldColBrickFull", args))
			break;
		x = (int16_t)rnd();
		y = (int16_t)rnd();
		z = (int16_t)rnd();
		if (rnd() & 1) {
			x = rndr(-1000, 64*512+1000);
			y = rndr(-1000, 25*256+1000);
			z = rndr(-1000, 64*512+1000);
		}
		snprintf(args, sizeof args, "%d %d %d", x, y, z);
		ra = (uint8_t)acall(wcb, 3, (int16_t)x, (int16_t)y, (int16_t)z);
		rc = WorldColBrick(x, y, z);
		if (ra != rc) {
			fail("WorldColBrick %s: ours %#x, the assembly's %#x", args, rc, ra);
			break;
		}
		if (!samemap("WorldColBrick", args))
			break;
		x = rndr(-256, 64*512-257);
		y = rndr(-1000, 25*256-1);
		z = rndr(-256, 64*512-257);
		snprintf(args, sizeof args, "%d %d %d", x, y, z);
		ra = acall(code, 3, x, y, z);
		rc = WorldCodeBrick(x, y, z);
		if (ra != rc) {
			fail("WorldCodeBrick %s: ours %#x, the assembly's %#x", args, rc, ra);
			break;
		}
		if (!samemap("WorldCodeBrick", args))
			break;
		m = rndn(Nblock);
		ra = acall(adr, 1, m);
		p = GetAdrBlock(m);
		if (ra != (int32_t)(uintptr_t)p) {
			fail("GetAdrBlock %d", m);
			break;
		}
		x = rndr(-100, 100);
		y = rndr(-100, 100);
		z = rndr(-100, 100);
		acall(m2s, 3, x, y, z);
		Map2Screen(x, y, z);
		if ((int32_t)get32((uint8_t*)lookup("XScreen")) != XScreen || (int32_t)get32((uint8_t*)lookup("YScreen")) != YScreen) {
			fail("Map2Screen %d %d %d", x, y, z);
			break;
		}
		column(col, rndn(8) == 0 ? rndr(1, 4000) : 25);
		fill(ad, sizeof ad);
		memcpy(cd, ad, sizeof ad);
		if (rnd() & 1) {
			acall(dec, 2, (int32_t)(uintptr_t)col, (int32_t)(uintptr_t)ad);
			DecompColonne(col, cd);
			snprintf(args, sizeof args, "DecompColonne");
		} else {
			acall(mix, 2, (int32_t)(uintptr_t)col, (int32_t)(uintptr_t)ad);
			MixteColonne(col, cd);
			snprintf(args, sizeof args, "MixteColonne");
		}
		if (memcmp(ad, cd, sizeof ad) != 0) {
			fail("%s: ours and the assembly's differ", args);
			break;
		}
	}
	printf("grille: %d rounds\n", r);
}

/*
 * WorldColBrickFull at positions in, on the edges of and outside the map,
 * under heights from -300 to 3000: LBA.EXE's assembly returned AL over a
 * coordinate's upper bytes outside the map, and for a height of 0 or less
 * tested every cell up to the top. The build's must give ours and leave
 * the map coordinates ours.
 */
static int
fixcolfull(void)
{
	uintptr_t full;
	int32_t a[4], rc, ra;
	char args[128];
	int r, k, out, low;

	full = lookup("WorldColBrickFull");
	for (k = 0; k < 3; k++)
		amap[k] = (uint8_t*)lookup(mapnames[k]);
	scene();
	BufCube = cube;
	TabBlock = blocks;
	put32((uint8_t*)lookup("BufCube"), (uint32_t)(uintptr_t)cube);
	put32((uint8_t*)lookup("TabBlock"), (uint32_t)(uintptr_t)blocks);
	out = low = 0;
	for (r = 0; r < Rounds; r++) {
		a[0] = rndr(-3000, 64*512+3000);
		a[1] = rndr(-2000, 25*256+2000);
		a[2] = rndr(-3000, 64*512+3000);
		a[3] = rndn(2) == 0 ? rndr(-300, 0) : rndr(-300, 3000);
		snprintf(args, sizeof args, "%d %d %d %d", a[0], a[1], a[2], a[3]);
		ra = acall(full, 4, a[0], a[1], a[2], a[3]);
		rc = WorldColBrickFull(a[0], a[1], a[2], a[3]);
		if (ra != rc) {
			fail("fixes: WorldColBrickFull %s: ours %#x, the assembly's %#x", args, rc, ra);
			return 0;
		}
		if (!samemap("fixes: WorldColBrickFull", args))
			return 0;
		out += XMap >= 64 || ZMap >= 64 || YMap <= -1;
		low += a[3] <= 0;
	}
	printf("fixes: colfull, %d positions, %d outside the map, %d under a height of 0 or less, the same\n",
		r, out, low);
	return 1;
}

/* grille_a's fixes (asmoracle.c's fixes). */
int
fixgrille(void)
{
	return fixcolfull();
}
