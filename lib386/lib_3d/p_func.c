#include <stddef.h>
#include <stdint.h>
#include "../lib_sys/adeline.h"
#include "../lib_sys/lib_sys.h"
#include "lib_3d.h"

/*
 * What p_func.asm gave the game: values that go from a start to an end
 * over a time (T_REAL_VALUE), rules of three, square roots, distances,
 * the angle of a vector and a rotation in 2D. Angles turn in 1024 steps,
 * sines are 14-bit (P_SinTab).
 */

int32_t	Distance;
int32_t	EX0;
int32_t	EY0;

/* InitRealValue is p_func.asm's InitRealValue: from start to end in time ticks from now. */
void
InitRealValue(int16_t start, int16_t end, int16_t time, T_REAL_VALUE *p)
{
	p->StartValue = start;
	p->EndValue = end;
	p->TimeValue = time;
	p->MemoTicks = TimerRef;
}

/* InitRealAngle is p_func.asm's InitRealAngle: InitRealValue of two angles. */
void
InitRealAngle(int16_t start, int16_t end, int16_t time, T_REAL_VALUE *p)
{
	p->StartValue = start & 1023;
	p->EndValue = end & 1023;
	p->TimeValue = time;
	p->MemoTicks = TimerRef;
}

/*
 * InitRealAngleConst is p_func.asm's InitRealAngleConst: from angle start
 * to angle end, time ticks a quarter turn, the time from the distance
 * between them the shorter way round.
 */
void
InitRealAngleConst(int32_t start, int32_t end, int32_t time, T_REAL_VALUE *p)
{
	uint16_t a, e, d;

	a = start & 1023;
	e = end & 1023;
	p->StartValue = a;
	p->EndValue = e;
	d = (uint16_t)(a-e) << 6;
	if (d & 0x8000)
		d = -d;
	d >>= 6;
	p->TimeValue = (uint32_t)d * (uint32_t)time >> 8;
	p->MemoTicks = TimerRef;
}

/*
 * GetRealValue is p_func.asm's GetRealValue: where the value is now,
 * its time ended (and cleared) at the end. The ticks elapsed are a
 * word, the reckoning 16-bit and signed, as the assembly's was.
 */
int16_t
GetRealValue(T_REAL_VALUE *p)
{
	uint16_t t;
	int32_t q;

	if (p->TimeValue == 0)
		return p->EndValue;
	t = TimerRef - p->MemoTicks;
	if (t >= p->TimeValue) {
		p->TimeValue = 0;
		return p->EndValue;
	}
	/*
	 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16 bits. This
	 * divides in 32 bits and keeps the low 16.
	 */
	q = (int32_t)(int16_t)(p->EndValue - p->StartValue) * (int16_t)t / (int16_t)p->TimeValue;
	return (int16_t)(q + p->StartValue);
}

/* GetRealAngle is p_func.asm's GetRealAngle: GetRealValue of an angle, the shorter way round, not wrapped to a turn. */
int16_t
GetRealAngle(T_REAL_VALUE *p)
{
	uint16_t t;
	int16_t d;
	int32_t q;

	if (p->TimeValue == 0)
		return p->EndValue;
	t = TimerRef - p->MemoTicks;
	if (t >= p->TimeValue) {
		p->TimeValue = 0;
		return p->EndValue;
	}
	d = p->EndValue - p->StartValue;
	if (d < -512)
		d += 1024;
	else if (d > 512)
		d -= 1024;
	/*
	 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16 bits. This
	 * divides in 32 bits and keeps the low 16.
	 */
	q = (int32_t)d * (int16_t)t / (int16_t)p->TimeValue;
	return (int16_t)(q + p->StartValue);
}

/* RegleTrois32 is p_func.asm's RegleTrois32: step of nbstep from v1 to v2, v2 if nbstep is not positive. */
int32_t
RegleTrois32(int32_t v1, int32_t v2, int32_t nbstep, int32_t step)
{
	int64_t q;

	if (nbstep <= 0)
		return v2;
	/*
	 * FIXED: LBA.EXE's idiv faults on a quotient past 32 bits. This
	 * divides in 64 bits and keeps the low 32.
	 */
	q = (int64_t)(int32_t)((uint32_t)v2 - (uint32_t)v1) * step / nbstep;
	return (uint32_t)q + (uint32_t)v1;
}

/* BoundRegleTrois is p_func.asm's BoundRegleTrois: RegleTrois32 held between v1 and v2. */
int32_t
BoundRegleTrois(int32_t v1, int32_t v2, int32_t nbstep, int32_t step)
{
	int64_t q;

	if (step <= 0)
		return v1;
	if (step >= nbstep)
		return v2;
	q = (int64_t)(int32_t)((uint32_t)v2 - (uint32_t)v1) * step / nbstep;
	return (uint32_t)q + (uint32_t)v1;
}

/*
 * Sqr is p_func.asm's Sqr, which took and gave EAX: the integer square
 * root of x, digit by digit two bits at a time from the top, as the
 * assembly worked it out.
 */
uint32_t
Sqr(uint32_t x)
{
	uint32_t hi, lo, r;
	int b, sh, n;

	if (x <= 3)
		return x != 0;
	for (b = 31; (x>>b & 1) == 0; b--)
		;
	sh = (33 - b) & ~1;
	hi = x >> (32 - sh);
	lo = x << sh;
	r = 1;
	hi--;
	for (n = b >> 1; n > 0; n--) {
		hi = hi<<2 | lo>>30;
		lo <<= 2;
		r <<= 2;
		if (hi < r) {
			r >>= 1;
			continue;
		}
		r++;
		if (hi >= r) {
			hi -= r;
			r++;
		}
		r >>= 1;
	}
	return r;
}

/* Distance2D is p_func.asm's Distance2D: the distance between x0,y0 and x1,y1, its squares on 32 bits. */
int32_t
Distance2D(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	uint32_t dx, dy;

	dx = (uint32_t)x1 - (uint32_t)x0;
	dy = (uint32_t)y1 - (uint32_t)y0;
	return Sqr(dx*dx + dy*dy);
}

/* Distance3D is p_func.asm's Distance3D: Distance2D in three dimensions. */
int32_t
Distance3D(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
{
	uint32_t dx, dy, dz;

	dx = (uint32_t)x1 - (uint32_t)x0;
	dy = (uint32_t)y1 - (uint32_t)y0;
	dz = (uint32_t)z1 - (uint32_t)z0;
	return Sqr(dx*dx + dy*dy + dz*dz);
}

/*
 * GetAngle is p_func.asm's GetAngle: the angle of x1,z1 seen from x0,z0,
 * Distance their distance. The sine of the angle to the nearer axis is
 * sought in the table between steps 384 and 640 by halving entries, the
 * nearer of the last two taken.
 */
int32_t
GetAngle(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
	int32_t dx, dz, big, small, a;
	uint32_t xx, zz, lo, hi, mid;
	int16_t s, w;

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
	Distance = Sqr(xx + zz);
	if (Distance == 0)
		return 0;
	s = (int32_t)((uint32_t)small << 14) / Distance;
	/*
	 * FIXED: LBA.EXE halves byte addresses in its search, not entries,
	 * so that its last steps read words across two entries and half the
	 * angles come out a step off the nearer one. This halves entries and
	 * takes the nearer of the last two. Taken from DotEmu's Android 1.06.
	 */
	lo = 384;
	hi = 640;
	for (;;) {
		mid = (lo + hi) >> 1;
		w = P_SinTab[mid];
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
	if (s <= (P_SinTab[lo] + P_SinTab[hi]) >> 1)
		lo = hi;
found:
	a = lo - 256;
	if (big < 0)
		a = -a;
	if (big & 1)
		a = 256 - a;
	return a & 1023;
}

/*
 * Rot2D is p_func.asm's Rot2D: x,y turned by angle into EX0,EY0. Its
 * products are shifted by 15 where the sines are 14-bit, so a vector
 * turned comes out halved, but for angle 0, which leaves it whole.
 */
void
Rot2D(int32_t x, int32_t y, int32_t angle)
{
	int32_t s, c;

	if (angle == 0) {
		EX0 = x;
		EY0 = y;
		return;
	}
	s = P_SinTab[angle & 1023];
	c = P_SinTab[((uint32_t)angle+256) & 1023];
	EX0 = (int32_t)((uint32_t)x*c - (uint32_t)y*s) >> 15;
	EY0 = (int32_t)((uint32_t)c*y + (uint32_t)s*x) >> 15;
}
