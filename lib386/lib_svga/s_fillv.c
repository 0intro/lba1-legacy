#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * The fillers of s_fillv.asm: the lines of a polygon that ComputePoly
 * or ComputeSphere left in TabVerticG and TabVerticD, from Ymin to Ymax,
 * filled in Log in one of nine ways, Screen_X bytes a line. A line whose
 * right end is left of its left is skipped, the Gouraud fillers moving
 * on past it in TabCoulG and TabCoulD.
 */

enum
{
	Ntype = 9,	/* MAX_TYPE_POLY */
};

typedef struct Fill Fill;

struct Fill
{
	uint8_t	*line;
	WORD	*l;	/* TabVerticG, TabVerticD from Ymin */
	WORD	*r;
	WORD	*cl;	/* TabCoulG, TabCoulD from Ymin */
	WORD	*cr;
	uint32_t	n;	/* lines */
	int32_t	col;
};

typedef void (*Filler)(Fill*);

static void triste(Fill*);
static void tele(Fill*);
static void copper(Fill*);
static void bopper(Fill*);
static void marbre(Fill*);
static void trans(Fill*);
static void trame(Fill*);
static void gouraud(Fill*);
static void dith(Fill*);
static void triche(Fill*);

static Filler details[3][Ntype] = {
	{triste, triste, copper, bopper, marbre, trans, trame, triche, triche},
	{triste, tele, copper, bopper, marbre, trans, trame, gouraud, gouraud},
	{triste, tele, copper, bopper, marbre, trans, trame, gouraud, dith},
};
static Filler jump[Ntype] = {triste, tele, copper, bopper, marbre, trans, trame, gouraud, dith};

/* SetFillDetails is s_fillv.asm's SetFillDetails: the fillers of level 0 (fewest), 1 or 2. */
void
SetFillDetails(int32_t level)
{
	if ((uint32_t)level > 2)
		level = 2;
	memcpy(jump, details[level], sizeof jump);
}

/* FillVertic is s_fillv.asm's FillVertic and FillVertic_A: the lines filled by the filler of type with col. */
void
FillVertic(int32_t type, int32_t col)
{
	Fill f;
	uint32_t y;

	y = (uint16_t)Ymin;
	f.line = Log + TabOffLine[y];
	f.l = TabVerticG + y;
	f.r = TabVerticD + y;
	f.cl = TabCoulG + y;
	f.cr = TabCoulD + y;
	f.n = (uint16_t)Ymax - y + 1;
	f.col = col;
	jump[type](&f);
}

/* The next line's ends, and whether to fill it: the width in 16 bits, 0 if skipped. */
static uint32_t
ends(Fill *f, uint8_t **p)
{
	uint16_t l, r;

	l = *f->l++;
	r = *f->r++;
	*p = f->line + l;
	if (r < l)
		return 0;
	/*
	 * FIXED: LBA.EXE's fillers take a line from x 0 to 65535 for one
	 * of 0 pixels, which Copper draws, stepping its colour, or of 65536,
	 * 64 or 128 KB. Its width, 0 in 16 bits, skips it as a line turned
	 * over.
	 */
	return (uint16_t)(r - l + 1);
}

/* SVGAPolyTriste: one colour. */
static void
triste(Fill *f)
{
	uint8_t *p;
	uint32_t w;

	do {
		w = ends(f, &p);
		if (w != 0)
			memset(p, f->col, w);
		f->line += Screen_X;
	} while (--f->n != 0);
}

/* A line of Copper: a first byte to an even address, then the rest. */
static void
copperline(uint8_t *p, uint32_t w, uint8_t c)
{
	if ((uintptr_t)p & 1) {
		*p++ = c;
		w--;
	}
	/*
	 * FIXED: LBA.EXE fills, of the rest past the dwords, only the pair
	 * the 2 bit gives (and cl, 2), so that a line whose last pixel lies
	 * at an even x loses it. This fills the line whole, as Bopper does.
	 * DotEmu and the Classic fill it whole too, but move the colour on
	 * the lines they skip, which LBA.EXE's steps keep here.
	 */
	memset(p, c, w);
}

/* SVGAPolyCopper: the colour up a line at a time through its ramp of 16, then down again. */
static void
copper(Fill *f)
{
	uint8_t *p, c;
	uint32_t w;

	c = f->col;
down:
	w = ends(f, &p);
	if (w == 0)
		goto nextdown;
	copperline(p, w, c);
	c++;
	if ((c & 15) == 0)
		goto turnup;
nextdown:
	f->line += Screen_X;
	if (--f->n != 0)
		goto down;
	return;
up:
	w = ends(f, &p);
	if (w != 0)
		copperline(p, w, c);
turnup:
	c--;
	if ((c & 15) == 0)
		goto nextdown;
	f->line += Screen_X;
	if (--f->n != 0)
		goto up;
}

/* A line of Bopper: a first byte to an even address, then the rest. */
static void
bopperline(uint8_t *p, uint32_t w, uint8_t c)
{
	if ((uintptr_t)p & 1) {
		*p++ = c;
		w--;
	}
	memset(p, c, w);
}

/* SVGAPolyBopper: Copper with each colour two lines high. */
static void
bopper(Fill *f)
{
	uint8_t *p, c, k;
	uint32_t w;

	c = f->col;
	k = 2;
down:
	w = ends(f, &p);
	if (w == 0)
		goto nextdown;
	bopperline(p, w, c);
	if (--k != 0)
		goto nextdown;
	k = 2;
	c++;
	if ((c & 15) == 0)
		goto turnup;
nextdown:
	f->line += Screen_X;
	if (--f->n != 0)
		goto down;
	return;
up:
	w = ends(f, &p);
	if (w == 0)
		goto turnup;
	bopperline(p, w, c);
	if (--k != 0)
		goto nextup;
turnup:
	k = 2;
	c--;
	if ((c & 15) == 0)
		goto nextdown;
nextup:
	f->line += Screen_X;
	if (--f->n != 0)
		goto up;
}

/*
 * SVGAPolyMarbre: across each line from col's low byte to its high,
 * the colour in the low byte of a word, its fraction in the high, whose
 * carry comes into the colour at the next pixel. A line one pixel wide
 * gets the high byte.
 */
static void
marbre(Fill *f)
{
	uint8_t *p, c0, c1;
	uint16_t l, r, step, v;
	uint32_t w, k, s;
	int16_t q;
	int cf;

	c0 = f->col;
	c1 = f->col >> 8;
	do {
		l = *f->l++;
		r = *f->r++;
		p = f->line + l;
		/*
		 * FIXED: LBA.EXE divides by the width of a line from x 0 to
		 * 65535, 0 in 16 bits, and faults. Skip it as a line turned
		 * over. DotEmu's Android 1.06, which compares a line's ends
		 * signed, skips it too.
		 */
		if (r == l)
			*p = c1;
		else if (r > l && (uint16_t)(r - l + 1) != 0) {
			w = r - l + 1;
			q = (int16_t)(uint16_t)((c1<<8) - (c0<<8) + 1) / (int16_t)w;
			step = (uint16_t)q<<8 | (uint16_t)q>>8;
			v = (uint16_t)q<<8 | c0;
			cf = 0;
			for (k = 0; k < w; k++) {
				p[k] = v;
				s = (uint32_t)v + step + cf;
				cf = s >> 16;
				v = s;
			}
		}
		f->line += Screen_X;
	} while (--f->n != 0);
}

/* SVGAPolyTele: noise of the 4 colours from col, from a seed each call, the pixel's x and the seed rolled. */
static void
tele(Fill *f)
{
	uint8_t *p;
	uint16_t l, r, a, b;
	uint32_t w, k;

	b = 17371;
	do {
		l = *f->l++;
		r = *f->r++;
		if (r >= l) {
			p = f->line + l;
			w = (uint32_t)r - l + 1;
			a = l;
			for (k = 0; k < w; k++) {
				a += b;
				a = (a & 0xFF00) | (uint8_t)((a & 3) + f->col);
				p[k] = a;
				b = b<<2 | b>>14;
				b++;
			}
		}
		f->line += Screen_X;
	} while (--f->n != 0);
}

/* SVGAPolyTrans: the upper nibble of col over what is there. */
static void
trans(Fill *f)
{
	uint8_t *p, c;
	uint32_t w, k;

	c = f->col & 0xF0;
	do {
		w = ends(f, &p);
		if (w != 0) {
			if (w & 1) {
				*p = (*p & 0x0F) | c;
				p++;
			}
			for (k = 0; k < (w>>1)*2; k++)
				p[k] = (p[k] & 0x0F) | c;
		}
		f->line += Screen_X;
	} while (--f->n != 0);
}

/*
 * SVGAPolyTrame: every other pixel, half the line's width of them, from
 * the first or the second as the line's address and a bit turned at
 * each line drawn say. A line one pixel wide is left.
 */
static void
trame(Fill *f)
{
	uint8_t *p;
	uint32_t w, k;
	int odd;

	odd = 0;
	do {
		w = ends(f, &p) >> 1;
		if (w != 0) {
			odd ^= 1;
			if (((uintptr_t)p & 1) != (uintptr_t)odd)
				p++;
			for (k = 0; k < w; k++)
				p[2*k] = f->col;
		}
		f->line += Screen_X;
	} while (--f->n != 0);
}

/* SVGAPolyTriche: each line in the integer part of its left intensity. */
static void
triche(Fill *f)
{
	uint8_t *p;
	uint32_t w;

	do {
		w = ends(f, &p);
		if (w != 0)
			memset(p, *f->cl >> 8, w);
		/*
		 * FIXED: LBA.EXE moves on in TabCoulG only past a line it
		 * draws, so that after a line it skips each takes the
		 * intensity of the line above. This moves on past every line.
		 */
		f->cl++;
		f->line += Screen_X;
	} while ((uint16_t)--f->n != 0);
}

/*
 * The shade halfway between a line's intensities b on the left and a on
 * the right, as the Gouraud fillers take it.
 */
static uint8_t
mean(uint16_t a, uint16_t b)
{
	/*
	 * FIXED: LBA.EXE takes the mean of the two unsigned, so that
	 * halfway from FExxh to 3xxh is 81h. This goes from b by half the
	 * difference signed, the short way round.
	 */
	return (uint16_t)(b + ((int16_t)(a - b) >> 1)) >> 8;
}

/*
 * SVGAPolyGouraud: the intensity from the left's to the right's, the
 * step the difference over the width less one. A line of 2 or 3 pixels
 * takes the ends and their mean, one of 1 the mean.
 */
static void
gouraud(Fill *f)
{
	uint8_t *p;
	uint16_t l, r, a, b, step, v;
	uint32_t n, k;

	do {
		l = *f->l++;
		r = *f->r++;
		p = f->line + l;
		/*
		 * FIXED: LBA.EXE moves on in TabCoulG and TabCoulD only past a
		 * line it draws, so that after a line it skips each takes the
		 * intensities of the line above. This moves on past every line.
		 */
		b = *f->cl++;
		a = *f->cr++;
		if (r >= l) {
			n = (uint16_t)(r - l);
			if (n == 0)
				*p = mean(a, b);
			else if (n <= 2) {
				p[n] = a >> 8;
				if (n == 2)
					p[1] = mean(a, b);
				*p = b >> 8;
			} else if ((int16_t)(a - b) >= 0) {
				/*
				 * FIXED: LBA.EXE steps the shade up when the
				 * right end's is the left's or above, unsigned
				 * (jc), so that from FExxh to 3xxh it runs down
				 * through every shade. This and the branch below
				 * take the short way round, as the difference
				 * signed says.
				 */
				/*
				 * FIXED: LBA.EXE counts the pairs of a line
				 * from x 0 to 65535 in 16 bits, 65536 of them.
				 * These count its 65536 pixels in 32 bits.
				 */
				step = (uint16_t)(a - b) / n;
				for (v = b, k = 0; k <= n; k++, v += step)
					p[k] = v >> 8;
			} else {
				step = (uint16_t)(b - a) / n;
				for (v = b, k = 0; k <= n; k++, v -= step)
					p[k] = v >> 8;
			}
		}
		f->line += Screen_X;
	} while (--f->n != 0);
}

/* rol on a byte, its count as cl gave it */
static uint8_t
rol8(uint8_t v, unsigned c)
{
	c = (c & 31) % 8;
	return c == 0 ? v : (uint8_t)(v << c | v >> (8-c));
}

/*
 * SVGAPolyDith: Gouraud dithered, the step signed: each pixel the
 * integer part of its intensity added to a byte of error, which is
 * rolled by what is left to draw at each pair of pixels.
 */
static void
dith(Fill *f)
{
	uint8_t *p, e;
	uint16_t l, r, si, d, dx;
	uint32_t n, cnt;

	do {
		l = *f->l++;
		r = *f->r++;
		p = f->line + l;
		/*
		 * FIXED: LBA.EXE moves on in TabCoulG and TabCoulD only past a
		 * line it draws, so that after a line it skips each takes the
		 * intensities of the line above. This moves on past every line.
		 */
		d = *f->cr++;
		si = *f->cl++;
		if (r < l)
			goto next;
		if (r == l) {
			*p = mean(d, si);
			goto next;
		}
		d -= si;
		n = (uint16_t)(r - l);
		dx = si;
		if (n <= 2) {
			cnt = (n + 1) >> 1;
			if ((n + 1) & 1) {
				e = rol8(dx, cnt);
				d = (uint16_t)((int16_t)d >> 1);
				dx = (uint16_t)(e + si);
				*p++ = dx >> 8;
				si += d;
			}
			dx = (uint16_t)((uint8_t)dx + si);
			p[0] = dx >> 8;
			si += d;
			e = rol8(dx, cnt);
			dx = (uint16_t)(e + si);
			p[1] = dx >> 8;
			goto next;
		}
		/*
		 * FIXED: LBA.EXE's idiv faults on -32768 by -1, and it counts
		 * the pairs of a line from x 0 to 65535 in 16 bits, 65536 of
		 * them. C divides in int, keeping the low 16 bits, and counts
		 * in 32.
		 */
		d = (int16_t)d / (int16_t)n;
		cnt = (n + 1) >> 1;
		if ((n + 1) & 1) {
			e = rol8(dx, cnt);
			dx = (uint16_t)(e + si);
			*p = dx >> 8;
			if (cnt == 0)
				goto next;
			p++;
			goto start2;
		}
		goto start;
	pair:
		p += 2;
	start2:
		si += d;
	start:
		dx = (uint16_t)((uint8_t)dx + si);
		p[0] = dx >> 8;
		si += d;
		e = rol8(dx, cnt);
		dx = (uint16_t)(e + si);
		p[1] = dx >> 8;
		if (--cnt != 0)
			goto pair;
	next:
		f->line += Screen_X;
	} while (--f->n != 0);
}
