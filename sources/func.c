#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"
#include "../sys/dat.h"

/*
 * What func.asm gave the game. Its calls to DOS (GetHDFreeSize,
 * ComputeTime) are the port's, and nothing calls SimpleInitMcga.
 */

/* SmallSort is func.asm's SmallSort: n entries of size bytes, by the signed word each starts with, smallest first. */
void
SmallSort(void *list, int32_t n, int32_t size)
{
	uint8_t *p, *q, *best, t;
	int16_t key, k;
	int32_t i, j;

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
		 * FIXED: LBA.EXE swaps up to 65536 dwords on a size under 4,
		 * and counts them in 16 bits. This swaps the bytes of size/4
		 * dwords, 0 under 4, and counts in 32 bits.
		 */
		if (best != NULL)
			for (j = 0; j < size/4*4; j++) {
				t = p[j];
				p[j] = best[j];
				best[j] = t;
			}
	}
}

/* CopyBlockMCGA is func.asm's CopyBlockMCGA: x0,y0 to x1,y1 of src, 640 wide, to the start of dst, 320 wide. xd and yd go unused. */
void
CopyBlockMCGA(int32_t x0, int32_t y0, int32_t x1, int32_t y1, void *src, int32_t xd, int32_t yd, void *dst)
{
	uint8_t *s, *d;
	int32_t h;

	USED(xd);
	USED(yd);
	s = (uint8_t*)src + TabOffLine[y0] + x0;
	d = dst;
	/*
	 * FIXED: LBA.EXE copies 2^32 lines on a height of 0 or less. This loop
	 * ends at once on such a height.
	 */
	for (h = y1 - y0 + 1; h > 0; h--) {
		memcpy(d, s, x1-x0+1);
		s += 640;
		d += 320;
	}
}

/* ShadeBox is func.asm's ShadeBox: x0,y0 to x1,y1 of Log darkened by dec within each colour's ramp of 16, clipped. */
void
ShadeBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t dec)
{
	uint8_t *p, d, lo;
	int32_t x, h;

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
	/*
	 * FIXED: LBA.EXE shades 2^32 lines on a height of 0 or less, and 2^32
	 * pixels on a width of 0 or less. These loops end at once on such a
	 * height or width.
	 */
	for (h = y1 - y0 + 1; h > 0; h--) {
		for (x = x0; x <= x1; x++, p++) {
			lo = *p & 0x0F;
			*p = (*p & 0xF0) + (lo < d ? 0 : lo-d);
		}
		p += Screen_X - (x1 - x0 + 1);
	}
}
