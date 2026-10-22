#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * CopyMask is cpymask.asm's CopyMask: through mask num of bank, the
 * pixels of src at x,y copied to the same place in Log. A mask has a
 * graph's header (see graph_a.c), then a line at a time a count of runs
 * and the runs, alternately of pixels to leave and to copy. Clipped, the
 * pixels come from Screen whatever src is, lines go 640 apart, and the
 * state machine below is the assembly's: its counts are bytes, compared
 * as its flags say, unsigned or, at two places, signed.
 */
void
CopyMask(int32_t num, int32_t x, int32_t y, void *bank, void *src)
{
	uint8_t *m, *s, *d, *sl, *dl, nb, nl, cl, left, r;
	uint32_t moff;
	int32_t w, h, x1, y1, off, npix;
	int borrow;

	memcpy(&moff, (uint8_t*)bank + 4*num, 4);
	m = (uint8_t*)bank + moff;
	x += m[2];
	y += m[3];
	w = m[0];
	h = m[1];
	m += 4;
	x1 = x + w - 1;
	y1 = y + h - 1;
	if (x >= ClipXmin && y >= ClipYmin && x1 <= ClipXmax && y1 <= ClipYmax) {
		s = (uint8_t*)src + TabOffLine[y] + x;
		d = Log + TabOffLine[y] + x;
		nl = h;
		do {
			nb = *m++;
			for (;;) {
				s += *m;
				d += *m;
				if (--nb == 0) {
					m++;
					break;
				}
				cl = m[1];
				m += 2;
				memcpy(d, s, cl);
				s += cl;
				d += cl;
				if (--nb == 0)
					break;
			}
			s += Screen_X - w;
			d += Screen_X - w;
		} while (--nl != 0);
		return;
	}
	if (x > ClipXmax || y > ClipYmax || x1 < ClipXmin || y1 < ClipYmin)
		return;
	if (y < ClipYmin) {
		for (off = ClipYmin - y; off > 0; off--)
			m += 1 + *m;
		y = ClipYmin;
	}
	if (y1 > ClipYmax)
		y1 = ClipYmax;
	off = x < ClipXmin ? ClipXmin - x : 0;
	npix = x1 - x - off + 1;
	if (x1 > ClipXmax)
		npix -= x1 - ClipXmax;
	dl = Log + TabOffLine[y] + x + off;
	sl = Screen + TabOffLine[y] + x + off;
	nl = y1 + 1 - y;
	for (;;) {
		cl = off;
		left = npix;
		s = sl;
		d = dl;
		nb = *m;
	l0:
		m++;
		if (--nb == 0)
			goto nextline;
		borrow = cl < *m;
		cl -= *m;
		if (borrow)
			goto rptj;
		if (cl == 0)
			goto okdraw;
		m++;
		nb--;
		borrow = cl < *m;
		cl -= *m;
		if (borrow)
			goto rgtc;
		if (cl == 0)
			goto okjump2;
		if (nb != 0)
			goto l0;
		goto nextline;
	rptj:
		r = left + cl;
		borrow = (int8_t)left + (int8_t)cl <= 0;
		left = r;
		if (borrow)
			goto aplus;
		cl = -cl;
		goto okdraw2;
	rgtc:
		cl = -cl;
		borrow = left < cl;
		left -= cl;
		if (borrow)
			goto cpt;
		memcpy(d, s, cl);
		d += cl;
		s += cl;
		if (left == 0)
			goto aplus;
	okjump2:
		if (nb == 0)
			goto nextline;
	okjump:
		m++;
		if (--nb == 0)
			goto nextline;
		cl = *m;
		borrow = (int8_t)left <= (int8_t)cl;
		left -= cl;
		if (borrow)
			goto aplus;
	okdraw2:
		d += cl;
		s += cl;
	okdraw:
		m++;
		nb--;
		cl = *m;
		borrow = left < cl;
		left -= cl;
		if (borrow)
			goto cpt;
		memcpy(d, s, cl);
		d += cl;
		s += cl;
		if (left == 0)
			goto aplus;
		if (nb != 0)
			goto okjump;
		goto nextline;
	cpt:
		cl += left;
		memcpy(d, s, cl);
	aplus:
		m += nb;
	nextline:
		if (--nl == 0)
			return;
		m++;
		sl += 640;
		dl += 640;
	}
}
