#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * The polygons of s_poly.asm. A polygon is NbPolyPoints points in
 * TabPoly, each a colour word (its low byte the intensity for Gouraud),
 * x and y. ComputePoly clips it and leaves, for each line from Ymin to
 * Ymax, its left x in TabVerticG and right in TabVerticD, and for
 * Gouraud the intensity at each end in TabCoulG and TabCoulD, 8.8.
 */

enum
{
	Npoly = 3*32 + 8,
	Nline = 480,
	Gouraud = 7,	/* POLY_GOURAUD */
};

WORD	Xmin, Ymin, Xmax, Ymax;
WORD	TypePoly;
WORD	NbPolyPoints;
/*
 * FIXED: LBA.EXE gives TabPoly 97 words and TabPolyClip 96, back to
 * back, so that a polygon of 32 points puts the copy of its first point
 * that closes it past TabPoly, where its clipping writes before reading
 * it. Each has 104, room for 33 points and that copy.
 */
WORD	TabPoly[Npoly] = {0x1234};
WORD	TabPolyClip[Npoly];
WORD	TabVerticG[Nline];
WORD	TabVerticD[Nline];
WORD	TabCoulG[Nline];
WORD	TabCoulD[Nline];
WORD	TabX1[Nline];
WORD	TabY1[Nline];

static uint16_t boucle;
static uint16_t newboucle;
static uint16_t flagrencadre;
static WORD *offtabpoly[2];

/*
 * A line of the circle when it is clipped: its ends held to the clip
 * box, or, if it lies wholly beside it, Ymax brought above it (a line
 * below the centre) or Ymin below it (above).
 */
static void
span(int32_t line, int32_t l, int32_t r, int below)
{
	/*
	 * FIXED: LBA.EXE tests the line against Ymin and Ymax in 16 bits and
	 * writes it at its 32-bit index, so that a radius from 32768 with a
	 * centre far below, or near 65535 with any, writes about 128 KB on
	 * either side of the tables. This tests the line in 32 bits.
	 */
	if (line > Ymax || line < Ymin)
		return;
	if (l < ClipXmin)
		l = ClipXmin;
	else if (l > ClipXmax)
		goto outside;
	TabVerticG[line] = l;
	if (r > ClipXmax)
		r = ClipXmax;
	else if (r < ClipXmin)
		goto outside;
	TabVerticD[line] = r;
	return;
outside:
	if (below)
		Ymax = line - 1;
	else
		Ymin = line + 1;
}

/*
 * ComputeSphere is s_poly.asm's ComputeSphere and ComputeSphere_A: the
 * lines of the circle of radius r about xc,yc into TabVerticG and
 * TabVerticD, by the midpoint steps, a step down taken on the carry of
 * an unsigned sum as the assembly took it. 0 if nothing shows.
 */
int32_t
ComputeSphere(int32_t xc, int32_t yc, int32_t r)
{
	int32_t x, y, v, clip;
	uint32_t d;

	y = r;
	d = -(uint32_t)r;
	clip = 0;
	v = yc - y;
	if (v > ClipYmax)
		return 0;
	if (v < ClipYmin) {
		clip++;
		v = ClipYmin;
	}
	Ymin = v;
	v = yc + y;
	if (v < ClipYmin)
		return 0;
	if (v > ClipYmax) {
		clip++;
		v = ClipYmax;
	}
	Ymax = v;
	v = xc + y;
	if (v < ClipXmin)
		return 0;
	if (v > ClipXmax)
		clip++;
	v = xc - y;
	if (v > ClipXmax)
		return 0;
	if (v < ClipXmin)
		clip++;
	for (x = 0; x <= y; x++) {
		if (clip == 0) {
			TabVerticD[yc+x] = xc + y;
			TabVerticG[yc+x] = xc - y;
			TabVerticG[yc-x] = xc - y;
			TabVerticD[yc-x] = xc + y;
		} else {
			span(yc+x, xc-y, xc+y, 1);
			span(yc-x, xc-y, xc+y, 0);
		}
		if (d + (uint32_t)x >= d) {
			d += x;
			continue;
		}
		d += x;
		if (clip == 0) {
			TabVerticD[yc+y] = xc + x;
			TabVerticG[yc+y] = xc - x;
			TabVerticG[yc-y] = xc - x;
			TabVerticD[yc-y] = xc + x;
		} else {
			span(yc+y, xc-x, xc+x, 1);
			span(yc-y, xc-x, xc+x, 0);
		}
		y--;
		d -= y;
	}
	if (clip == 0)
		return 1;
	/*
	 * FIXED: LBA.EXE gives up a clipped circle that its clipping leaves
	 * one line high (jge nosphere). This draws that line. DotEmu and the
	 * Classic draw it too, from a circle they frame otherwise.
	 */
	return Ymin <= Ymax;
}

/* a*b/c as the assembly's 16-bit imul and idiv made it */
static int16_t
mul16div(int32_t a, int32_t b, int32_t c)
{
	/*
	 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16 bits. This
	 * divides in 32 bits and keeps the low 16.
	 */
	return (int16_t)((int32_t)(int16_t)a * (int16_t)b / (int16_t)c);
}

/*
 * The four clippers, s_poly.asm's ClipGauche, ClipDroit, ClipHaut and
 * ClipBas: the polygon of offtabpoly[0] cut by one side of the clip box
 * into offtabpoly[1], which becomes offtabpoly[0], closed by a copy of
 * its first point. How many points it has left. Where an edge crosses
 * the side, the new point is reckoned from the end of the greater
 * coordinate along the axis, and gets an intensity only for Gouraud:
 * otherwise its colour word keeps what the buffer held. axis is the
 * index of the coordinate cut, 1 for x, 2 for y. low says whether what
 * is below edge is cut away (the left and top sides) or above.
 */
static uint16_t
clipside(int axis, int32_t edge, int low)
{
	WORD *s, *d, *t, c0, a0, b0, c1, a1, b1, rc, ra, rb, oc, oa, ob;
	int16_t e;
	int out0, out1;

	newboucle = 0;
	flagrencadre = 1;
	s = offtabpoly[0];
	d = offtabpoly[1];
	e = edge;
	c0 = s[0];
	a0 = s[axis];
	b0 = s[3-axis];
	s += 3;
	do {
		c1 = s[0];
		a1 = s[axis];
		b1 = s[3-axis];
		s += 3;
		out0 = low ? a0 < e : a0 > e;
		out1 = low ? a1 < e : a1 > e;
		if (!out0) {
			d[0] = c0;
			d[axis] = a0;
			d[3-axis] = b0;
			d += 3;
			newboucle++;
		}
		if (out0 != out1) {
			if (a1 < a0) {
				rc = c0;
				ra = a0;
				rb = b0;
				oc = c1;
				oa = a1;
				ob = b1;
			} else {
				rc = c1;
				ra = a1;
				rb = b1;
				oc = c0;
				oa = a0;
				ob = b0;
			}
			d[axis] = e;
			d[3-axis] = rb + mul16div(e - ra, ob - rb, oa - ra);
			if ((uint16_t)TypePoly >= Gouraud)
				d[0] = rc + mul16div(oc - rc, e - ra, oa - ra);
			d += 3;
			newboucle++;
		}
		c0 = c1;
		a0 = a1;
		b0 = b1;
	} while (--boucle != 0);
	t = offtabpoly[0];
	offtabpoly[0] = offtabpoly[1];
	offtabpoly[1] = t;
	memcpy(d, offtabpoly[0], 3*sizeof(WORD));
	boucle = newboucle;
	return boucle;
}

/*
 * One edge of the polygon into the line tables: x0,y0 to x1,y1, the
 * left side going down (TabVerticG, TabCoulG), the right going up
 * (TabVerticD, TabCoulD). An edge is gone along with x growing on the
 * left and shrinking on the right, its lines written up the table when
 * that is from its lower end, as the assembly's direction flag had it.
 * x steps in 16.16, its fraction kept in the upper half of a word whose
 * carry goes into x at the next step (adc, sbb). An intensity in 8.8.
 */
static void
edge(int left, WORD x0, WORD y0, WORD x1, WORD y1, uint8_t i0, uint8_t i1)
{
	WORD *tab, *coul, t;
	uint32_t a, d, q, r, n, start;
	uint16_t dy, w, v, step;
	uint64_t s;
	uint8_t ti;
	int dir, cf, swap;

	if (left) {
		dy = y1 - y0;
		swap = x0 > x1;
		dir = swap ? -1 : 1;
	} else {
		dy = y0 - y1;
		swap = x0 < x1;
		dir = swap ? 1 : -1;
	}
	if (swap) {
		t = x0;
		x0 = x1;
		x1 = t;
		t = y0;
		y0 = y1;
		y1 = t;
		ti = i0;
		i0 = i1;
		i1 = ti;
	}
	start = (uint16_t)y0;
	tab = left ? TabVerticG : TabVerticD;
	w = left ? x1 - x0 : x0 - x1;
	q = ((uint32_t)w << 16) / dy;
	r = ((uint32_t)w << 16) % dy;
	d = q<<16 | q>>16;
	v = (uint16_t)r >> 1;
	v = left ? v + 0x7FFF : 0x7FFF - v;
	a = (uint32_t)v<<16 | (uint16_t)x0;
	/* the first step's carry, x put back: add or sub, then the low word undone */
	if (left) {
		s = (uint64_t)a + d;
		cf = s >> 32;
	} else {
		cf = a < d;
		s = (uint32_t)(a - d);
	}
	a = ((uint32_t)s & 0xFFFF0000) | (a & 0xFFFF);
	for (n = 0; n <= dy; n++) {
		tab[start + dir*(int32_t)n] = (uint16_t)a;
		if (left) {
			s = (uint64_t)a + d + cf;
			cf = s >> 32;
			a = s;
		} else {
			s = (uint64_t)d + cf;
			cf = s > a;
			a -= s;
		}
	}
	if ((uint16_t)TypePoly < Gouraud)
		return;
	coul = left ? TabCoulG : TabCoulD;
	/*
	 * FIXED: LBA.EXE steps the shade up when the end's byte is the
	 * start's or above (jc), so that from FEh to 3 it runs down through
	 * every shade. This steps the short way round the 256 shades, as
	 * the difference of the bytes signed says. DotEmu steps by the
	 * difference of the points' words, rounding its edges otherwise.
	 */
	if ((int8_t)(i1 - i0) >= 0) {
		w = (uint16_t)((uint8_t)(i1 - i0) << 8);
		step = w / dy;
		v = (uint16_t)(i0 << 8 | (uint8_t)(((uint8_t)(w % dy) >> 1) + 0x7F));
		for (n = 0; n <= dy; n++) {
			coul[start + dir*(int32_t)n] = v;
			v += step;
		}
	} else {
		w = (uint16_t)((uint8_t)(i0 - i1) << 8);
		step = w / dy;
		v = (uint16_t)(i0 << 8 | (uint8_t)(0x7F - ((uint8_t)(w % dy) >> 1)));
		for (n = 0; n <= dy; n++) {
			coul[start + dir*(int32_t)n] = v;
			v -= step;
		}
	}
}

/*
 * ComputePoly is s_poly.asm's ComputePoly and ComputePoly_A: the polygon
 * of TabPoly framed, clipped and into the line tables. 0 if nothing of
 * it shows. Ymin and Ymax are framed again after clipping, and a
 * polygon then a line high is not drawn.
 */
int32_t
ComputePoly(void)
{
	WORD *p, x0, y0, x1, y1;
	int16_t xmin, ymin, xmax, ymax;
	uint32_t n;
	uint8_t i0, i1;

	/*
	 * FIXED: LBA.EXE frames a polygon of 0 points as one of 2^32,
	 * reading TabPoly past its end. One of 0 points returns 0 at
	 * once. Taken from DotEmu's Android 1.06.
	 */
	if (NbPolyPoints == 0)
		return 0;
	offtabpoly[0] = TabPoly;
	offtabpoly[1] = TabPolyClip;
	boucle = NbPolyPoints;
	flagrencadre = 0;
	xmin = ymin = 32767;
	xmax = ymax = -32768;
	p = TabPoly;
	n = (uint16_t)NbPolyPoints;
	do {
		if (p[1] < xmin)
			xmin = p[1];
		if (p[1] > xmax)
			xmax = p[1];
		if (p[2] < ymin)
			ymin = p[2];
		if (p[2] > ymax)
			ymax = p[2];
		p += 3;
	} while (--n != 0);
	memcpy(p, TabPoly, 3*sizeof(WORD));
	Xmin = xmin;
	Ymin = ymin;
	Xmax = xmax;
	Ymax = ymax;
	if (ymax < ymin)
		return 0;
	if (Xmin < (int16_t)ClipXmin) {
		if (Xmax < (int16_t)ClipXmin || clipside(1, ClipXmin, 1) == 0)
			return 0;
	}
	if (Xmax > (int16_t)ClipXmax) {
		if (Xmin > (int16_t)ClipXmax || clipside(1, ClipXmax, 0) == 0)
			return 0;
	}
	if (Ymin < (int16_t)ClipYmin) {
		if (Ymax < (int16_t)ClipYmin || clipside(2, ClipYmin, 1) == 0)
			return 0;
	}
	if (Ymax > (int16_t)ClipYmax) {
		if (Ymin > (int16_t)ClipYmax || clipside(2, ClipYmax, 0) == 0)
			return 0;
	}
	if (flagrencadre) {
		ymin = 32767;
		ymax = -32768;
		p = offtabpoly[0];
		n = boucle;
		do {
			if (p[2] < ymin)
				ymin = p[2];
			if (p[2] > ymax)
				ymax = p[2];
			p += 3;
		} while (--n != 0);
		Ymin = ymin;
		Ymax = ymax;
		if (ymin >= ymax)
			return 0;
	}
	p = offtabpoly[0];
	i1 = p[0];
	x0 = p[1];
	y0 = p[2];
	p += 3;
	do {
		i0 = i1;
		i1 = p[0];
		x1 = p[1];
		y1 = p[2];
		p += 3;
		if (y1 != y0)
			edge(y1 > y0, x0, y0, x1, y1, i0, i1);
		x0 = x1;
		y0 = y1;
	} while (--boucle != 0);
	return 1;
}
