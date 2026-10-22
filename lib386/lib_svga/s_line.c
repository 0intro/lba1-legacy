#include <stddef.h>
#include <stdint.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/* a*b/c, where |a| is at most |c| */
static int32_t
muldiv(int32_t a, int32_t b, int32_t c)
{
	/*
	 * FIXED: LBA.EXE multiplies and divides the low 16 bits and keeps 16
	 * of the quotient: past 16 bits a clipped end lands anywhere, so that
	 * the clipping may never end, and a dx or dy that is a multiple of
	 * 65536 divides by 0. This multiplies in 64 bits and divides in 32,
	 * the quotient at most |b| while the coordinates lie within 2^30.
	 */
	return (int64_t)a * b / c;
}

/*
 * The sign of |ay|*dx less ax*|dy|, in products of 64 bits, ax at least
 * 0 and dx above 0: whether a line by dx, dy meets an edge in y ay away
 * after (1), with (0) or before (-1) one in x ax away.
 */
static int
crossing(int32_t ax, int32_t ay, int32_t dx, int32_t dy)
{
	int64_t a, b;

	a = (ay < 0 ? -(int64_t)ay : ay) * dx;
	b = (int64_t)ax * (dy < 0 ? -(int64_t)dy : dy);
	return (a > b) - (a < b);
}

/*
 * Line is s_line.asm's Line, and Line_A, its entry for p_ob_iso.asm:
 * x0,y0 to x1,y1 in Log in colour col. The ends are clipped one at a
 * time to the clip box, an end outside on both axes at the edge the line
 * crosses there, then the line goes by Bresenham's steps, Screen_X
 * bytes a line.
 */
void
Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t col)
{
	int32_t t, dx, dy, step;
	uint32_t e, d2, e2, n;
	int c0, c1;
	uint8_t *p;

	if (x0 > x1) {
		t = x0;
		x0 = x1;
		x1 = t;
		t = y0;
		y0 = y1;
		y1 = t;
	}
	for (;;) {
		c0 = 0;
		if (x0 < ClipXmin)
			c0 |= 1;
		else if (x0 > ClipXmax)
			return;
		if (y0 < ClipYmin)
			c0 |= 8;
		else if (y0 > ClipYmax)
			c0 |= 4;
		if (x1 < ClipXmin)
			return;
		c1 = 0;
		if (x1 > ClipXmax)
			c1 |= 2;
		if (y1 < ClipYmin)
			c1 |= 8;
		else if (y1 > ClipYmax)
			c1 |= 4;
		if (c0 & c1)
			return;
		if ((c0 | c1) == 0)
			break;
		dx = x1 - x0;
		dy = y1 - y0;
		/*
		 * FIXED: LBA.EXE clips an end outside on both axes in x, then
		 * in y from the point the x clip rounded, so that the rounding
		 * compounds: a shallow line through a box one line high can be
		 * rejected though it crosses the box, and one entering at a
		 * corner starts up to dx/dy pixels late. This clips such an end
		 * once, at the edge the line crosses there, the first end's
		 * later entry or the other's earlier exit, x on a tie.
		 */
		if ((c0 & 1) && (c0 & 12)
		&& crossing(ClipXmin - x0, ((c0 & 8) ? ClipYmin : ClipYmax) - y0, dx, dy) > 0)
			c0 &= ~1;
		if (c0 == 0 && (c1 & 2) && (c1 & 12)
		&& crossing(ClipXmax - x0, ((c1 & 8) ? ClipYmin : ClipYmax) - y0, dx, dy) < 0)
			c1 &= ~2;
		if (c0 & 1) {
			y0 += muldiv(ClipXmin - x0, dy, dx);
			x0 = ClipXmin;
		} else if (c0 & 8) {
			x0 += muldiv(ClipYmin - y0, dx, dy);
			y0 = ClipYmin;
		} else if (c0 & 4) {
			x0 += muldiv(ClipYmax - y0, dx, dy);
			y0 = ClipYmax;
		} else if (c1 & 2) {
			y1 = y0 + muldiv(ClipXmax - x0, dy, dx);
			x1 = ClipXmax;
		} else if (c1 & 8) {
			x1 = x0 + muldiv(ClipYmin - y0, dx, dy);
			y1 = ClipYmin;
		} else {
			x1 = x0 + muldiv(ClipYmax - y0, dx, dy);
			y1 = ClipYmax;
		}
	}
	p = Log + TabOffLine[y0] + x0;
	dx = x1 - x0;
	dy = y1 - y0;
	step = Screen_X;
	if (dy < 0) {
		step = -Screen_X;
		dy = -dy;
	}
	if (dx >= dy) {
		e = dx;
		d2 = 2 * dy;
		e2 = 2 * dx;
		for (n = dx + 1; n > 0; n--) {
			*p++ = col;
			if (e >= d2)
				e -= d2;
			else {
				e = e - d2 + e2;
				p += step;
			}
		}
	} else {
		e = dy;
		d2 = 2 * dx;
		e2 = 2 * dy;
		for (n = dy + 1; n > 0; n--) {
			*p = col;
			if (e >= d2) {
				e -= d2;
				p += step;
			} else {
				e = e - d2 + e2;
				p += step + 1;
			}
		}
	}
}
