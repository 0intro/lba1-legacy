#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asmoracle.h"

/*
 * p_func's angles, rotations and distances, rules of three, and values
 * and angles over time: ours and the assembly's (asmoracle.c's pfunc).
 */

enum
{
	Real = 10,	/* sizeof(T_REAL_VALUE) */
};

uint32_t	TimerRef;	/* p_func.c's clock */

uint32_t	Sqr(uint32_t);
void	InitRealValue(int16_t, int16_t, int16_t, void*);
void	InitRealAngle(int16_t, int16_t, int16_t, void*);
void	InitRealAngleConst(int32_t, int32_t, int32_t, void*);
int16_t	GetRealValue(void*);
int16_t	GetRealAngle(void*);
int32_t	RegleTrois32(int32_t, int32_t, int32_t, int32_t);
int32_t	BoundRegleTrois(int32_t, int32_t, int32_t, int32_t);
int32_t	Distance2D(int32_t, int32_t, int32_t, int32_t);
int32_t	Distance3D(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
int32_t	GetAngle(int32_t, int32_t, int32_t, int32_t);
void	Rot2D(int32_t, int32_t, int32_t);
extern int32_t	Distance, EX0, EY0;
extern int16_t	P_SinTab[1024];

/*
 * Whether LBA.EXE's GetRealValue, or with angle its GetRealAngle, faults
 * on the value p now: its 16-bit idiv of the change times the ticks
 * elapsed by the time gives a quotient past 16 bits.
 */
static int
realfaults(uint8_t *p, int angle)
{
	int16_t d;
	uint16_t t, tv;
	int32_t q;

	tv = get16(p+4);
	t = TimerRef - get32(p+6);
	if (tv == 0 || t >= tv)
		return 0;
	d = get16(p+2) - get16(p);
	if (angle && d < -512)
		d += 1024;
	else if (angle && d > 512)
		d -= 1024;
	q = (int32_t)d * (int16_t)t / (int16_t)tv;
	return q < -32768 || q > 32767;
}

/* A coordinate, small, middling or large. */
static int32_t
coord(void)
{
	switch (rndn(4)) {
	case 0:
		return rndr(-100, 100);
	case 1:
		return rndr(-20000, 20000);
	case 2:
		return rndr(-(1<<20), 1<<20);
	}
	return rnd();
}

/*
 * m1994angle is LBA.EXE's GetAngle, the tag's assembly, which seeks the
 * sine by halving byte addresses, not entries, so that its last halvings
 * read words across two entries.
 */
static int32_t
m1994angle(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
	int32_t dx, dz, big, small, d, a;
	uint32_t xx, zz, lo, hi, mid;
	int16_t s, w, v;

	dz = (uint32_t)z1 - (uint32_t)z0;
	dx = (uint32_t)x1 - (uint32_t)x0;
	xx = (uint32_t)dx * (uint32_t)dx;
	zz = (uint32_t)dz * (uint32_t)dz;
	if (xx >= zz) {
		big = dx & -2;
		small = dz;
	} else {
		big = dz | 1;
		small = dx;
	}
	d = Sqr(xx + zz);
	if (d == 0)
		return 0;
	s = (int32_t)((uint32_t)small << 14) / d;
	lo = 384 * 2;
	hi = 640 * 2;
	for (;;) {
		mid = (lo + hi) >> 1;
		memcpy(&w, (uint8_t*)P_SinTab + mid, 2);
		if (s > w)
			hi = mid;
		else {
			lo = mid;
			if (s == w)
				goto found;
		}
		if (hi-lo == 1)
			break;
	}
	memcpy(&w, (uint8_t*)P_SinTab + lo, 2);
	memcpy(&v, (uint8_t*)P_SinTab + hi, 2);
	if ((int16_t)((w + v) >> 1) <= s)
		lo = hi;
found:
	a = (lo - 256*2) >> 1;
	if (big < 0)
		a = -a;
	if (big & 1)
		a = 256 - a;
	return a & 1023;
}

/*
 * GetAngle of a vector of length 16384 for every small side k from
 * -16384 to 16384, its big side either way: the search's every s, where
 * an exact match ends it at once.
 */
static void
allangles(uintptr_t ga, int32_t *ad)
{
	int64_t b;
	int32_t k, y, z, x1;
	int i;

	for (k = -16384; k <= 16384; k++) {
		for (b = 0; b*b + (int64_t)k*k < (int64_t)16384*16384; b++)
			if (b < 16384 && (b+64)*(b+64) + (int64_t)k*k < (int64_t)16384*16384)
				b += 63;
		for (i = 0; i < 2; i++) {
			x1 = i ? -b : b;
			y = GetAngle(0, 0, x1, k);
			z = acall(ga, 4, 0, 0, x1, k);
			if (y != z || Distance != *ad) {
				fail("GetAngle 0 0 %d %d: ours %d (%d), the assembly's %d (%d)", x1, k, y, Distance, z, *ad);
				return;
			}
		}
	}
	printf("pfunc: GetAngle of every s, %d vectors\n", 2*32769);
}

void
testpfunc(void)
{
	uintptr_t ival, iang, iangc, gval, gang, rt, brt, d2, d3, ga, r2;
	int32_t *ad, *aex, *aey;
	uint32_t *atimer;
	uint8_t rc[Real], ra[Real];
	int32_t a[6], v1, v2, n, s, t, y, z;
	int r, i, k, angle;

	ival = lookup("InitRealValue");
	iang = lookup("InitRealAngle");
	iangc = lookup("InitRealAngleConst");
	gval = lookup("GetRealValue");
	gang = lookup("GetRealAngle");
	rt = lookup("RegleTrois32");
	brt = lookup("BoundRegleTrois");
	d2 = lookup("Distance2D");
	d3 = lookup("Distance3D");
	ga = lookup("GetAngle");
	r2 = lookup("Rot2D");
	ad = (int32_t*)lookup("Distance");
	aex = (int32_t*)lookup("EX0");
	aey = (int32_t*)lookup("EY0");
	atimer = (uint32_t*)lookup("TimerRef");
	for (r = 0; r < Rounds; r++) {
		for (i = 0; i < 6; i++)
			a[i] = coord();
		if (rndn(8) == 0)
			a[rndn(2)] = a[2+rndn(2)];
		/* a vector in eight of 3 at most on each axis, where the signs and parities decide */
		if (rndn(8) == 0) {
			a[2] = a[0] + rndr(-3, 3);
			a[3] = a[1] + rndr(-3, 3);
		}
		y = GetAngle(a[0], a[1], a[2], a[3]);
		z = acall(ga, 4, a[0], a[1], a[2], a[3]);
		if (y != z || Distance != *ad)
			fail("GetAngle %d %d %d %d: ours %d (%d), the assembly's %d (%d)", a[0], a[1], a[2], a[3],
				y, Distance, z, *ad);
		y = Distance2D(a[0], a[1], a[2], a[3]);
		z = acall(d2, 4, a[0], a[1], a[2], a[3]);
		if (y != z)
			fail("Distance2D %d %d %d %d: ours %d, the assembly's %d", a[0], a[1], a[2], a[3], y, z);
		y = Distance3D(a[0], a[1], a[2], a[3], a[4], a[5]);
		z = acall(d3, 6, a[0], a[1], a[2], a[3], a[4], a[5]);
		if (y != z)
			fail("Distance3D %d %d %d %d %d %d: ours %d, the assembly's %d", a[0], a[1], a[2], a[3], a[4], a[5], y, z);
		n = rndn(4) == 0 ? (int32_t)rnd() : rndr(-1, 2048);
		Rot2D(a[0], a[1], n);
		acall(r2, 3, a[0], a[1], n);
		if (EX0 != *aex || EY0 != *aey)
			fail("Rot2D %d %d %d: ours %d %d, the assembly's %d %d", a[0], a[1], n, EX0, EY0, *aex, *aey);
		v1 = rndr(-(1<<20), 1<<20);
		v2 = rndr(-(1<<20), 1<<20);
		n = rndr(-5, 2000);
		s = rndr(-4*(n > 0 ? n : 1), 4*(n > 0 ? n : 1));
		y = RegleTrois32(v1, v2, n, s);
		z = acall(rt, 4, v1, v2, n, s);
		if (y != z)
			fail("RegleTrois32 %d %d %d %d: ours %d, the assembly's %d", v1, v2, n, s, y, z);
		y = BoundRegleTrois(v1, v2, n, s);
		z = acall(brt, 4, v1, v2, n, s);
		if (y != z)
			fail("BoundRegleTrois %d %d %d %d: ours %d, the assembly's %d", v1, v2, n, s, y, z);
		*atimer = TimerRef = rnd();
		v1 = (int16_t)rnd();
		v2 = (int16_t)rnd();
		/* a time in eight of 32768 ticks or more, a negative word to the assembly */
		t = rndn(4) == 0 ? 0 : rndn(8) == 0 ? (int16_t)rndr(32768, 65535) : rndr(1, 32767);
		switch (rndn(3)) {
		case 0:
			InitRealValue(v1, v2, t, rc);
			acall(ival, 4, v1, v2, t, (int32_t)(uintptr_t)ra);
			break;
		case 1:
			InitRealAngle(v1, v2, t, rc);
			acall(iang, 4, v1, v2, t, (int32_t)(uintptr_t)ra);
			break;
		case 2:
			t = rndn(1000);
			InitRealAngleConst(v1, v2, t, rc);
			acall(iangc, 4, v1, v2, t, (int32_t)(uintptr_t)ra);
			break;
		}
		if (memcmp(rc, ra, Real) != 0)
			fail("InitReal %d %d %d: ours and the assembly's differ", v1, v2, t);
		for (k = 0; k < 4; k++) {
			TimerRef += rndn(2*((uint16_t)t+1));
			*atimer = TimerRef;
			angle = rnd() & 1;
			if (realfaults(rc, angle))
				continue;
			if (!angle) {
				y = GetRealValue(rc);
				z = (int16_t)acall(gval, 1, (int32_t)(uintptr_t)ra);
			} else {
				y = GetRealAngle(rc);
				z = (int16_t)acall(gang, 1, (int32_t)(uintptr_t)ra);
			}
			if (y != z || memcmp(rc, ra, Real) != 0)
				fail("GetReal at %u: ours %d, the assembly's %d", TimerRef, y, z);
		}
	}
	printf("pfunc: %d rounds\n", r);
	if (r == Rounds)
		allangles(ga, ad);
}

/*
 * GetAngle over the game's coordinates, where LBA.EXE's search of the
 * sine table gives half the angles a step off the nearer one: ours and
 * the build's assembly, which searches by entries.
 */
static int
fixangle(void)
{
	uintptr_t ga;
	int32_t a[4], y, z;
	int r, i, apart;

	ga = lookup("GetAngle");
	apart = 0;
	for (r = 0; r < 10*Rounds; r++) {
		for (i = 0; i < 4; i++)
			a[i] = rndr(0, 32767);
		y = GetAngle(a[0], a[1], a[2], a[3]);
		z = acall(ga, 4, a[0], a[1], a[2], a[3]);
		if (y != z) {
			fail("fixes: GetAngle %d %d %d %d: ours %d, the assembly's %d", a[0], a[1], a[2], a[3], y, z);
			return 0;
		}
		apart += y != m1994angle(a[0], a[1], a[2], a[3]);
	}
	printf("fixes: angle, %d angles, %d of them apart from LBA.EXE's, the same\n", r, apart);
	return 1;
}

/* p_func's fixes (asmoracle.c's fixes). */
int
fixfunc(void)
{
	return fixangle();
}
