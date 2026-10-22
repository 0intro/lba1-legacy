#include <stddef.h>
#include <stdint.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/* a*b/c as the assembly's 16-bit imul and idiv made it, sign-extended */
static int32_t
mul16div(int32_t a, int32_t b, int32_t c)
{
	/*
	 * FIXED: LBA.EXE's 16-bit idiv faults on a quotient past 16 bits. This
	 * divides in 32 bits and keeps the low 16.
	 */
	return (int16_t)((int32_t)(int16_t)a * (int16_t)b / (int16_t)c);
}

/*
 * Line is s_line.asm's Line, and Line_A, its entry for p_ob_iso.asm:
 * x0,y0 to x1,y1 in Log in colour col. The ends are clipped one at a
 * time to the clip box, then the line goes by Bresenham's steps, 640
 * bytes a line. Going mostly along y, a step of x is taken only when
 * adding to the error carries, as the assembly's adc took it: always,
 * but for a line its 16-bit clipping turned around.
 */
void
Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t col)
{
	int32_t t, dx, dy, step, carry;
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
		if (c0 & 1) {
			y0 += mul16div(ClipXmin - x0, dy, dx);
			x0 = ClipXmin;
		} else if (c0 & 8) {
			x0 += mul16div(ClipYmin - y0, dx, dy);
			y0 = ClipYmin;
		} else if (c0 & 4) {
			x0 += mul16div(ClipYmax - y0, dx, dy);
			y0 = ClipYmax;
		} else if (c1 & 2) {
			y1 = y0 + mul16div(ClipXmax - x0, dy, dx);
			x1 = ClipXmax;
		} else if (c1 & 8) {
			x1 = x0 + mul16div(ClipYmin - y0, dx, dy);
			y1 = ClipYmin;
		} else {
			x1 = x0 + mul16div(ClipYmax - y0, dx, dy);
			y1 = ClipYmax;
		}
	}
	p = Log + TabOffLine[y0] + x0;
	dx = x1 - x0;
	dy = y1 - y0;
	step = 640;
	if (dy < 0) {
		step = -640;
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
				e -= d2;
				carry = e + e2 < e;
				e += e2;
				p += step + carry;
			}
		}
	}
}
