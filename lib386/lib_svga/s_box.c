#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/* Box is s_box.asm's Box: x0,y0 to x1,y1 of Log filled with col, clipped. */
void
Box(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t col)
{
	uint8_t *p;
	int32_t h;

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
	/*
	 * FIXED: LBA.EXE fills 2^32 lines on a height of 0 or less, and faults
	 * on a negative width. Fill none. Taken from DotEmu's Android 1.06.
	 */
	if (x1 < x0)
		return;
	for (h = y1 - y0 + 1; h > 0; h--) {
		memset(p, col, x1-x0+1);
		p += Screen_X;
	}
}
