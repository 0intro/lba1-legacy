#include <stddef.h>
#include <stdint.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * The textured triangles of texture.asm, the holomap's globe. A
 * triangle is three points of TabPoly (a colour or intensity, x and y
 * each) with the texel of each point in TabText at the same places, in
 * 8.8, on a map 256 texels wide. Its edges go into the line tables, an
 * edge going down being on the left (TabVerticG, its texels in TabCoulG
 * and TabCoulD) and one going up on the right (TabVerticD, TabX1 and
 * TabY1). The lines are then filled from the map. The assembly named
 * TabVerticG, TabVerticD, TabCoulG and TabCoulD TabGauche, TabDroite,
 * TabX0 and TabY0.
 */

enum
{
	Noclip,
	Clip,
	Shade,	/* clipped, and darkened */
};

LONG	LYmin, LYmax;
WORD	TabText[32*3];

/*
 * The words of p from line y0 to y1 stepped from x0 to x1, as
 * texture.asm's A_FillPropNoClip (round) and AsmFillProp did: x in
 * 16.16, the fraction in the upper half of a word whose carry comes into
 * x at the next step (adc, sbb), starting from the remainder of the
 * step, halved and put past one half if round. The ends' x are
 * subtracted in 16 bits.
 */
static void
fillprop(WORD *p, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int round)
{
	uint32_t n, q, a, d;
	uint64_t s;
	uint16_t w, f;
	int32_t t;
	int cf, down;

	if (y0 > y1) {
		t = y0;
		y0 = y1;
		y1 = t;
		t = x0;
		x0 = x1;
		x1 = t;
	}
	n = (uint32_t)y1 - (uint32_t)y0;
	p += y0;
	down = x0 > x1;
	if (down)
		w = (uint16_t)x0 - (uint16_t)x1;
	else
		w = (uint16_t)x1 - (uint16_t)x0;
	q = ((uint32_t)w << 16) / n;
	f = ((uint32_t)w << 16) % n;
	if (round)
		f = (f >> 1) + 0x7FFF;
	d = q<<16 | q>>16;
	a = (uint32_t)f<<16 | (uint16_t)x0;
	cf = 0;
	n++;
	do {
		*p++ = (uint16_t)a;
		if (down) {
			s = (uint64_t)d + cf;
			cf = s > a;
			a -= s;
		} else {
			s = (uint64_t)a + d + cf;
			cf = s >> 32;
			a = s;
		}
	} while (--n != 0);
}

/* AsmFillPropNoClip is texture.asm's AsmFillPropNoClip: ptrdest's words from y0 to y1 stepped from x0 to x1. */
void
AsmFillPropNoClip(WORD *ptrdest, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	fillprop(ptrdest, x0, y0, x1, y1, 1);
}

/*
 * AsmFillProp is texture.asm's AsmFillProp: the same within ClipYmin
 * and ClipYmax, an end beyond them brought to the edge along the line,
 * and the fraction starting from the remainder alone.
 */
void
AsmFillProp(WORD *ptrdest, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int32_t t;

	if (y0 > y1) {
		t = y0;
		y0 = y1;
		y1 = t;
		t = x0;
		x0 = x1;
		x1 = t;
	}
	if (y0 > ClipYmax || y1 < ClipYmin)
		return;
	/*
	 * FIXED: LBA.EXE's idiv faults on a quotient past 32 bits. These
	 * divide in 64 bits and keep the low 32.
	 */
	if (y0 < ClipYmin) {
		if (y0 == y1)
			return;
		x0 += (int64_t)(ClipYmin - y0) * (x0 - x1) / (y0 - y1);
		y0 = ClipYmin;
	}
	if (y1 > ClipYmax) {
		if (y0 == y1)
			return;
		x1 += (int64_t)(ClipYmax - y1) * (x0 - x1) / (y0 - y1);
		y1 = ClipYmax;
	}
	/*
	 * FIXED: a clip box whose ClipYmax lies above its ClipYmin brings
	 * the ends to its edges in reverse, and LBA.EXE steps the count of
	 * lines between, negative, as some 2^32 words past the table. Such
	 * a box returns at once.
	 */
	if (y0 >= y1)
		return;
	fillprop(ptrdest, x0, y0, x1, y1, 0);
}

/*
 * The edges of the triangle of TabPoly into the tables, as texture.asm's
 * two triangles did: from point 0 to 1, 1 to 2 and 2 to 0, with the
 * texels of TabText (tex) or the colours of TabPoly, a flat edge in no
 * table. The lines they span in LYmin and LYmax.
 */
static void
triangle(int tex)
{
	int32_t x0, y0, x1, y1;
	WORD *u;
	int i, a, b;

	LYmin = 32000;
	LYmax = -32000;
	u = tex ? TabText : TabPoly;
	for (i = 0; i < 3; i++) {
		a = 3 * i;
		b = i == 2 ? 0 : a+3;
		x0 = (uint16_t)TabPoly[a+1];
		y0 = (uint16_t)TabPoly[a+2];
		x1 = (uint16_t)TabPoly[b+1];
		y1 = (uint16_t)TabPoly[b+2];
		if (y0 < y1) {
			if (y0 < LYmin)
				LYmin = y0;
			if (y1 > LYmax)
				LYmax = y1;
			fillprop(TabVerticG, x0, y0, x1, y1, 1);
			if (tex) {
				fillprop(TabCoulG, (uint16_t)u[a+1], y0, (uint16_t)u[b+1], y1, 1);
				fillprop(TabCoulD, (uint16_t)u[a+2], y0, (uint16_t)u[b+2], y1, 1);
			} else
				fillprop(TabCoulG, (uint16_t)u[a], y0, (uint16_t)u[b], y1, 1);
		} else if (y0 > y1) {
			if (y1 < LYmin)
				LYmin = y1;
			if (y0 > LYmax)
				LYmax = y0;
			fillprop(TabVerticD, x0, y0, x1, y1, 1);
			if (tex) {
				fillprop(TabX1, (uint16_t)u[a+1], y0, (uint16_t)u[b+1], y1, 1);
				fillprop(TabY1, (uint16_t)u[a+2], y0, (uint16_t)u[b+2], y1, 1);
			} else
				fillprop(TabCoulD, (uint16_t)u[a], y0, (uint16_t)u[b], y1, 1);
		}
	}
}

/* AsmTexturedTriangleNoClip is texture.asm's AsmTexturedTriangleNoClip: the triangle and its texels into the tables. */
void
AsmTexturedTriangleNoClip(void)
{
	triangle(1);
}

/* AsmGouraudTriangleNoClip is texture.asm's AsmGouraudTriangleNoClip: the triangle and its intensities, its lines also in Ymin and Ymax. */
void
AsmGouraudTriangleNoClip(void)
{
	triangle(0);
	Ymin = LYmin;
	Ymax = LYmax;
}

/*
 * The lines ymin to ymax of the tables filled from map, Screen_X bytes a
 * line, as texture.asm's three fillers did. A line of width w, its right
 * x less its left, is skipped if w is not above 0. The texel steps by
 * the difference of its ends plus one over w, of which 16 bits count.
 * Unclipped, the line is w pixels from its left x. Clipped, it goes to
 * its right x as well, within ClipXmin and ClipXmax (compared in 16
 * bits), the left's texels moved to ClipXmin in the tables themselves.
 * Shade takes dark from each texel's low nibble, down to 0.
 */
static void
filltext(int32_t ymin, int32_t ymax, uint8_t *map, int mode, uint8_t dark)
{
	uint8_t *line, *p, t;
	uint16_t n, w, a, c, k, tx, ty, sx, sy;
	int32_t y, l, r;

	line = Log + TabOffLine[ymin];
	n = ymax - ymin + 1;
	y = ymin;
	do {
		l = TabVerticG[y];
		r = TabVerticD[y];
		if (r - l <= 0)
			goto next;
		sy = ((int32_t)(uint16_t)TabY1[y] - (uint16_t)TabCoulD[y] + 1) / (r - l);
		sx = ((int32_t)(uint16_t)TabX1[y] - (uint16_t)TabCoulG[y] + 1) / (r - l);
		tx = TabCoulG[y];
		ty = TabCoulD[y];
		if (mode == Noclip) {
			p = line + l;
			w = r - l;
		} else {
			a = l;
			if ((int16_t)a < (int16_t)ClipXmin) {
				k = a - ClipXmin;
				tx -= (uint32_t)k * sx;
				ty -= (uint32_t)k * sy;
				TabCoulG[y] = tx;
				TabCoulD[y] = ty;
				a = ClipXmin;
			}
			c = r;
			if ((int16_t)c > (int16_t)ClipXmax)
				c = ClipXmax;
			w = c - a;
			if ((int16_t)w < 0)
				goto next;
			w++;
			p = line + a;
		}
		do {
			t = map[(ty & 0xFF00) | tx >> 8];
			if (mode == Shade) {
				if ((t & 15) < dark)
					t &= 0xF0;
				else
					t -= dark;
			}
			*p++ = t;
			tx += sx;
			ty += sy;
		} while (--w != 0);
	next:
		line += Screen_X;
		y++;
	} while (--n != 0);
}

/* FillTextPolyNoClip is texture.asm's FillTextPolyNoClip: the lines ymin to ymax textured from source. */
void
FillTextPolyNoClip(int32_t ymin, int32_t ymax, void *source)
{
	filltext(ymin, ymax, source, Noclip, 0);
}

/* FillTextPoly is texture.asm's FillTextPoly: the same, clipped. */
void
FillTextPoly(int32_t ymin, int32_t ymax, void *source)
{
	filltext(ymin, ymax, source, Clip, 0);
}

/* FillTextPolyShade is texture.asm's FillTextPolyShade: the same, darkened by dark. */
void
FillTextPolyShade(int32_t ymin, int32_t ymax, int32_t dark, void *source)
{
	filltext(ymin, ymax, source, Shade, dark);
}
