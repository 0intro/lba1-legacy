#include <stddef.h>
#include <stdint.h>
#include "../lib_sys/adeline.h"
#include "../lib_sys/lib_sys.h"
#include "lib_svga.h"

/*
 * The blocks of s_block.asm, each line copied forward four bytes at a
 * time and then one, as MovMem copies, so that an overlap goes as it
 * went.
 */

/* CopyBlock is s_block.asm's CopyBlock: x0,y0 to x1,y1 of src to xd,yd of dst. */
void
CopyBlock(int32_t x0, int32_t y0, int32_t x1, int32_t y1, void *src, int32_t xd, int32_t yd, void *dst)
{
	uint8_t *s, *d;
	int32_t w;
	uint32_t h;

	s = (uint8_t*)src + TabOffLine[y0] + x0;
	d = (uint8_t*)dst + TabOffLine[yd] + xd;
	w = x1 - x0 + 1;
	/* a height of 0 or less runs past the screen, as the assembly's count */
	h = y1 - y0 + 1;
	do {
		MovMem(s, d, w);
		s += Screen_X;
		d += Screen_X;
	} while (--h != 0);
}

/* SaveBlock is s_block.asm's SaveBlock: x0,y0 to x1,y1 of screen into buffer, its lines one after another. */
void
SaveBlock(void *screen, void *buffer, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	uint8_t *s, *d;
	int32_t w, h;

	s = (uint8_t*)screen + TabOffLine[y0] + x0;
	d = buffer;
	w = x1 - x0 + 1;
	/*
	 * FIXED: LBA.EXE copies 2^32 lines on a height of 0 or less, and
	 * faults on a negative width. Copy none. Taken from the Classic 3.2.3.
	 */
	if (w <= 0)
		return;
	for (h = y1 - y0 + 1; h > 0; h--) {
		MovMem(s, d, w);
		s += Screen_X;
		d += w;
	}
}

/* RestoreBlock is s_block.asm's RestoreBlock: what SaveBlock put in buffer back to x0,y0 to x1,y1 of screen. */
void
RestoreBlock(void *screen, void *buffer, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	uint8_t *s, *d;
	int32_t w, h;

	s = buffer;
	d = (uint8_t*)screen + TabOffLine[y0] + x0;
	w = x1 - x0 + 1;
	/*
	 * FIXED: LBA.EXE copies 2^32 lines on a height of 0 or less, and
	 * faults on a negative width. Copy none. Taken from the Classic 3.2.3.
	 */
	if (w <= 0)
		return;
	for (h = y1 - y0 + 1; h > 0; h--) {
		MovMem(s, d, w);
		s += w;
		d += Screen_X;
	}
}
