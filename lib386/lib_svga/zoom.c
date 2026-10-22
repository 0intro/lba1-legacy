#include <stddef.h>
#include <stdint.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * The scaler of zoom.asm, which the 1994 game left unlinked: each pixel
 * of the destination is the source pixel it falls on, the source walked
 * in 16.16 fixed point, its fraction carried in 16 bits.
 */

/*
 * ScaleBox is zoom.asm's ScaleBox: xs0,ys0 to xs1,ys1 of src drawn into
 * xd0,yd0 to xd1,yd1 of dst, xd1 above xd0 and yd1 above yd0. The step is
 * the source's size over the destination's less one, so that a size that
 * divides exactly reads a pixel past the source's right edge and a line
 * past its bottom. Lines go Screen_X apart in both, where the assembly
 * stepped the source through TabOffLine and the destination by 640. The
 * DOS builds link no zoom.asm, so that the records of the saves' pictures
 * hold it, where asmoracle holds the others.
 */
void
ScaleBox(int32_t xs0, int32_t ys0, int32_t xs1, int32_t ys1, void *src, int32_t xd0, int32_t yd0, int32_t xd1, int32_t yd1, void *dst)
{
	uint8_t *s, *p, *d;
	uint32_t xstep, ystep, xf, yf, t;
	int32_t w, h, n;

	d = (uint8_t*)dst + xd0 + TabOffLine[yd0];
	s = (uint8_t*)src + xs0 + TabOffLine[ys0];
	xstep = ((uint32_t)(xs1 - xs0 + 1) << 16) / (uint32_t)(xd1 - xd0);
	ystep = ((uint32_t)(ys1 - ys0 + 1) << 16) / (uint32_t)(yd1 - yd0);
	w = xd1 - xd0 + 1;
	yf = 0;
	for (h = yd1 - yd0 + 1; h > 0; h--) {
		p = s;
		xf = 0;
		for (n = w; n > 0; n--) {
			*d++ = *p;
			t = xf + (xstep & 0xFFFF);
			xf = t & 0xFFFF;
			p += (xstep >> 16) + (t >> 16);
		}
		d += Screen_X - w;
		t = yf + (ystep & 0xFFFF);
		yf = t & 0xFFFF;
		s += ((ystep >> 16) + (t >> 16)) * Screen_X;
	}
}
