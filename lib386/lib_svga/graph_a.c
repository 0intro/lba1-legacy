#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * A graph of a bank: at the bank's start, a table of the graphs'
 * offsets, and at a graph's, its width and height, its hot spot, then a
 * line at a time a count of blocks, each a byte and its data: 00 and n
 * give n+1 pixels left as they are, 01 (or 11) and n the n+1 pixels that
 * follow, and 10 and n a colour, n+1 of it. The counts are bytes, and a
 * count of 0 runs 256 times, as the assembly's does.
 */

static uint8_t*
graph(void *bank, int32_t num)
{
	uint32_t off;

	memcpy(&off, (uint8_t*)bank + 4*num, 4);
	return (uint8_t*)bank + off;
}

/*
 * AffGraph is graph_a.asm's AffGraph: graph num of bank, its hot spot at
 * x,y in Log. Clipped, each line is drawn into a buffer first and only
 * its pixels other than 0 are copied, to lines 640 apart: a graph drawn
 * whole draws its pixels of 0 and goes Screen_X apart.
 */
void
AffGraph(int32_t num, int32_t x, int32_t y, void *bank)
{
	static uint8_t clip[512];
	uint8_t *s, *d, *line, op, n, nb, nl;
	int32_t w, h, x1, y1, off, npix, i;
	uint32_t c;

	s = graph(bank, num);
	w = s[0];
	h = s[1];
	x += s[2];
	y += s[3];
	s += 4;
	x1 = x + w - 1;
	y1 = y + h - 1;
	if (x >= ClipXmin && y >= ClipYmin && x1 <= ClipXmax && y1 <= ClipYmax) {
		d = Log + TabOffLine[y] + x;
		nl = h;
		do {
			nb = *s++;
			do {
				op = *s;
				n = (op & 0x3F) + 1;
				if ((op & 0xC0) == 0)
					s++;
				else if (op & 0x40) {
					memcpy(d, s+1, n);
					s += n + 1;
				} else {
					memset(d, s[1], n);
					s += 2;
				}
				d += n;
			} while (--nb != 0);
			d += Screen_X - w;
		} while (--nl != 0);
		return;
	}
	if (x > ClipXmax || y > ClipYmax || x1 < ClipXmin || y1 < ClipYmin)
		return;
	if (y < ClipYmin) {
		nl = ClipYmin - y;
		do {
			nb = *s++;
			do {
				op = *s++;
				if (op & 0xC0)
					s += (op & 0x40 ? op & 0x3F : 0) + 1;
			} while (--nb != 0);
		} while (--nl != 0);
		y = ClipYmin;
	}
	if (y1 > ClipYmax)
		y1 = ClipYmax;
	off = x < ClipXmin ? ClipXmin - x : 0;
	npix = x1 - x + 1 - off;
	if (x1 > ClipXmax)
		npix -= x1 - ClipXmax;
	line = Log + TabOffLine[y] + x;
	nl = y1 - y + 1;
	do {
		d = clip;
		nb = *s++;
		do {
			op = *s++;
			n = (op & 0x3F) + 1;
			if ((op & 0xC0) == 0)
				memset(d, 0, n);
			else if (op & 0x40) {
				memcpy(d, s, n);
				s += n;
			} else
				memset(d, *s++, n);
			d += n;
		} while (--nb != 0);
		/* a clipped width of 0 or less runs past the line, as the assembly's count */
		for (i = 0, c = npix; ; i++) {
			if (clip[off+i] != 0)
				line[off+i] = clip[off+i];
			if (--c == 0)
				break;
		}
		line += 640;
	} while (--nl != 0);
}

/* GetDxDyGraph is graph_a.asm's GetDxDyGraph: graph num's width and height. */
void
GetDxDyGraph(int32_t num, int32_t *dx, int32_t *dy, void *bank)
{
	uint8_t *s;

	s = graph(bank, num);
	*dx = s[0];
	*dy = s[1];
}
