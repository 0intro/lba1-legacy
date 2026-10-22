#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * The masks of mask_a.asm: a bank's table of offsets, and at each a
 * mask, a graph's header (see graph_a.c), then a line at a time a count
 * of runs and the runs, alternately of pixels to leave and of pixels to
 * paint in one colour. Clipped, a line is painted into a buffer first
 * and its pixels of 0 are left, so that a mask of colour 0 paints
 * nothing, and lines go 640 apart.
 */

static uint8_t colmask;

/* CoulMask is mask_a.asm's CoulMask: the colour masks are painted in. */
void
CoulMask(int32_t col)
{
	colmask = col;
}

/* AffMask is mask_a.asm's AffMask, and AffMask_Asm, its entry for the fonts: mask num of bank, its hot spot at x,y in Log. */
void
AffMask(int32_t num, int32_t x, int32_t y, void *bank)
{
	static uint8_t clip[512];
	uint8_t *m, *d, *line, nb, nl, n;
	uint32_t off;
	int32_t w, h, x1, y1, ox, npix, i;
	uint32_t c;

	memcpy(&off, (uint8_t*)bank + 4*num, 4);
	m = (uint8_t*)bank + off;
	x += m[2];
	y += m[3];
	w = m[0];
	h = m[1];
	m += 4;
	x1 = x + w - 1;
	y1 = y + h - 1;
	if (x >= ClipXmin && y >= ClipYmin && x1 <= ClipXmax && y1 <= ClipYmax) {
		d = Log + TabOffLine[y] + x;
		nl = h;
		do {
			nb = *m++;
			for (;;) {
				d += *m;
				if (--nb == 0) {
					m++;
					break;
				}
				n = m[1];
				m += 2;
				memset(d, colmask, n);
				d += n;
				if (--nb == 0)
					break;
			}
			d += Screen_X - w;
		} while (--nl != 0);
		return;
	}
	if (x > ClipXmax || y > ClipYmax || x1 < ClipXmin || y1 < ClipYmin)
		return;
	if (y < ClipYmin) {
		for (i = ClipYmin - y; i > 0; i--)
			m += 1 + *m;
		y = ClipYmin;
	}
	if (y1 > ClipYmax)
		y1 = ClipYmax;
	ox = x < ClipXmin ? ClipXmin - x : 0;
	npix = x1 - x - ox + 1;
	if (x1 > ClipXmax)
		npix -= x1 - ClipXmax;
	line = Log + TabOffLine[y] + x;
	nl = y1 - y + 1;
	do {
		d = clip;
		nb = *m++;
		for (;;) {
			n = *m;
			memset(d, 0, n);
			d += n;
			if (--nb == 0)
				break;
			n = m[1];
			m += 2;
			memset(d, colmask, n);
			d += n;
			if (--nb == 0) {
				m--;
				break;
			}
		}
		m++;
		/* a clipped width of 0 or less runs past the line, as the assembly's count */
		for (i = 0, c = npix; ; i++) {
			if (clip[ox+i] != 0)
				line[ox+i] = clip[ox+i];
			if (--c == 0)
				break;
		}
		line += 640;
	} while (--nl != 0);
}

/* GetDxDyMask is mask_a.asm's GetDxDyMask: mask num's width and height. */
void
GetDxDyMask(int32_t num, int32_t *dx, int32_t *dy, void *bank)
{
	uint32_t off;
	uint8_t *m;

	memcpy(&off, (uint8_t*)bank + 4*num, 4);
	m = (uint8_t*)bank + off;
	*dx = m[0];
	*dy = m[1];
}
