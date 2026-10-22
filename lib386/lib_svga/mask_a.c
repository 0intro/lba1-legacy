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
 * nothing, and lines go Screen_X apart.
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

	memcpy(&off, (uint8_t*)bank + 4*num, 4);
	m = (uint8_t*)bank + off;
	x += m[2];
	y += m[3];
	w = m[0];
	h = m[1];
	m += 4;
	/*
	 * FIXED: LBA.EXE counts a mask's lines and a line's runs in bytes
	 * it decrements before testing them, so that a mask of 0 lines
	 * draws 256 and a line of 0 runs reads 256 runs from the bytes
	 * after it. A mask of 0 lines returns at once, and a line of 0 runs
	 * is passed over. Taken from DotEmu's Android 1.06, but for a line
	 * of 0 runs, where DotEmu moves the lines after it left by the
	 * mask's width or, clipped, copies the buffer of the line before.
	 */
	if (h == 0)
		return;
	x1 = x + w - 1;
	y1 = y + h - 1;
	if (x >= ClipXmin && y >= ClipYmin && x1 <= ClipXmax && y1 <= ClipYmax) {
		d = Log + TabOffLine[y] + x;
		nl = h;
		do {
			nb = *m++;
			if (nb == 0) {
				d += Screen_X;
				continue;
			}
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
		if (nb == 0) {
			line += Screen_X;
			continue;
		}
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
		/*
		 * FIXED: LBA.EXE runs 2^32 pixels on a clipped width of 0 or
		 * less. This loop ends at once on such a width.
		 */
		for (i = 0; i < npix; i++)
			if (clip[ox+i] != 0)
				line[ox+i] = clip[ox+i];
		line += Screen_X;
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
