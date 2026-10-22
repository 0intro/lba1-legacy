#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"
#include "../sys/dat.h"

/*
 * What func.asm gave the game, but its calls to DOS (SimpleInitMcga,
 * GetHDFreeSize, ComputeTime).
 */

/* SmallSort is func.asm's SmallSort: n entries of size bytes, by the signed word each starts with, smallest first. */
void
SmallSort(void *list, int32_t n, int32_t size)
{
	uint8_t *p, *q, *best, t;
	int16_t key, k;
	int32_t i, j, m;
	uint16_t c;

	if (n <= 1)
		return;
	p = list;
	for (i = n - 1; i > 0; i--, p += size) {
		memcpy(&key, p, 2);
		best = NULL;
		for (j = i, q = p + size; j > 0; j--, q += size) {
			memcpy(&k, q, 2);
			if (k < key) {
				key = k;
				best = q;
			}
		}
		/*
		 * the entries swapped a dword at a time, size/4 of them counted
		 * in 16 bits as the assembly's DX, so that a size under 4 swaps
		 * 65536
		 */
		if (best != NULL) {
			c = (uint32_t)size >> 2;
			j = 0;
			do {
				for (m = j + 4; j < m; j++) {
					t = p[j];
					p[j] = best[j];
					best[j] = t;
				}
			} while (--c != 0);
		}
	}
}

/* CopyBlockMCGA is func.asm's CopyBlockMCGA: x0,y0 to x1,y1 of src, 640 wide, to the start of dst, 320 wide. xd and yd go unused. */
void
CopyBlockMCGA(int32_t x0, int32_t y0, int32_t x1, int32_t y1, void *src, int32_t xd, int32_t yd, void *dst)
{
	uint8_t *s, *d;
	uint32_t h;

	USED(xd);
	USED(yd);
	s = (uint8_t*)src + TabOffLine[y0] + x0;
	d = dst;
	/* a height of 0 or less runs past the screen, as the assembly's count */
	h = y1 - y0 + 1;
	do {
		memcpy(d, s, x1-x0+1);
		s += 640;
		d += 320;
	} while (--h != 0);
}

/* ShadeBox is func.asm's ShadeBox: x0,y0 to x1,y1 of Log darkened by dec within each colour's ramp of 16, clipped. */
void
ShadeBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t dec)
{
	uint8_t *p, d, lo;
	uint32_t w, h, n;

	if (x0 > ClipXmax || x1 < ClipXmin || y0 > ClipYmax || y1 < ClipYmin)
		return;
	if (x0 < ClipXmin)
		x0 = ClipXmin;
	if (x1 > ClipXmax)
		x1 = ClipXmax;
	if (y0 < ClipYmin)
		y0 = ClipYmin;
	if (y1 > ClipYmax)
		y1 = ClipYmax;
	p = Log + TabOffLine[y0] + x0;
	d = dec;
	/* a height or a width of 0 or less runs past the box, as the assembly's counts */
	h = y1 - y0 + 1;
	w = x1 - x0 + 1;
	do {
		n = w;
		do {
			lo = *p & 0x0F;
			*p = (*p & 0xF0) + (lo < d ? 0 : lo-d);
			p++;
		} while (--n != 0);
		p += Screen_X - (x1 - x0 + 1);
	} while (--h != 0);
}
