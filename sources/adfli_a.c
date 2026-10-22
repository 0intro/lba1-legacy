#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * The frames of the movies (FLA), of adfli_a.asm, drawn in Log as a
 * 320-wide picture. BlackFrame and CopyFrame step 640 a line, as the
 * assembly did.
 */

/*
 * DrawFrame is adfli_a.asm's DrawFrame: dy lines dx apart, each a count
 * of runs, a run a byte: negative, the -n pixels that follow, else n
 * and a colour, n of it.
 */
void
DrawFrame(void *frame, int32_t dx, int32_t dy)
{
	uint8_t *s, *d, *line, nb, n;

	s = frame;
	line = Log;
	/*
	 * FIXED: LBA.EXE draws 2^32 lines for a frame of 0, from Log on.
	 * Draw none. Taken from DotEmu's Android 1.06.
	 */
	if (dy == 0)
		return;
	do {
		nb = *s++;
		d = line;
		if (nb != 0)
			do {
				n = *s++;
				if (n & 0x80) {
					n = -n;
					memcpy(d, s, n);
					s += n;
				} else
					memset(d, *s++, n);
				d += n;
			} while (--nb != 0);
		line += dx;
	} while (--dy != 0);
}

/*
 * UpdateFrame is adfli_a.asm's UpdateFrame: from the line its first word
 * says, as many lines as its second, each a count of runs, a run a byte
 * of pixels to skip and a byte: positive, the n pixels that follow, else
 * a colour, -n of it.
 */
void
UpdateFrame(void *frame, int32_t dx)
{
	uint8_t *s, *d, *line, nb, n;
	uint16_t y, nl;

	s = frame;
	memcpy(&y, s, 2);
	memcpy(&nl, s+2, 2);
	s += 4;
	line = Log + (int32_t)y*dx;
	/*
	 * FIXED: LBA.EXE draws 65536 lines for a frame of 0, from its first
	 * line on. Draw none. Taken from DotEmu's Android 1.06.
	 */
	if (nl == 0)
		return;
	do {
		nb = *s++;
		d = line;
		if (nb != 0)
			do {
				d += *s++;
				n = *s++;
				if (n & 0x80) {
					n = -n;
					memset(d, *s++, n);
				} else {
					memcpy(d, s, n);
					s += n;
				}
				d += n;
			} while (--nb != 0);
		line += dx;
	} while (--nl != 0);
}

/* BlackFrame is adfli_a.asm's BlackFrame: 200 lines of 320 cleared, 640 apart. */
void
BlackFrame(void)
{
	int y;

	for (y = 0; y < 200; y++)
		memset(Log + 640*y, 0, 320);
}

/* CopyFrame is adfli_a.asm's CopyFrame: 200 lines of 320 from src, 640 apart. */
void
CopyFrame(void *src)
{
	int y;

	for (y = 0; y < 200; y++)
		memcpy(Log + 640*y, (uint8_t*)src + 320*y, 320);
}
