#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib_sys/adeline.h"
#include "lib_svga.h"

/*
 * The fonts of font_a.asm: a font is a bank of masks (see mask_a.c),
 * one a character, painted in the masks' colour, with interleave pixels
 * between characters and interspace for a space.
 */

static uint8_t *ptrfont;
static int32_t interleave = 1;
static int32_t interspace = 10;

static uint8_t*
car(int32_t c)
{
	uint32_t off;

	memcpy(&off, ptrfont + 4*c, 4);
	return ptrfont + off;
}

/* CoulFont is font_a.asm's CoulFont: the masks' colour. */
void
CoulFont(int32_t col)
{
	CoulMask(col & 0xFF);
}

/* SetFont is font_a.asm's SetFont. */
void
SetFont(void *font, int32_t il, int32_t is)
{
	ptrfont = font;
	interleave = il;
	interspace = is;
}

/* SizeFont is font_a.asm's SizeFont: the width of s, as Font draws it. */
int32_t
SizeFont(void *s)
{
	uint8_t *p;
	uint32_t dx;

	dx = 0;
	for (p = s; *p != 0; p++)
		if (*p == ' ')
			dx += interspace;
		else {
			/*
			 * FIXED: LBA.EXE adds the interleave, then the interleave
			 * with its low byte replaced by the character's width, so
			 * that an interleave of 256 or more measures wider than
			 * Font draws. Add the width alone. Taken from DotEmu's
			 * Android 1.06.
			 */
			dx += interleave;
			dx += car(*p)[0];
		}
	return dx;
}

/* CarFont is font_a.asm's CarFont: character c at x,y, its width and interleave returned. */
int32_t
CarFont(int32_t x, int32_t y, int32_t c)
{
	if (c == ' ')
		return interspace;
	AffMask(c, x, y, ptrfont);
	return car(c)[0] + interleave;
}

/* Font is font_a.asm's Font: s at x,y. */
void
Font(int32_t x, int32_t y, void *s)
{
	uint8_t *p;
	int32_t w;

	if (ptrfont == NULL)
		return;
	for (p = s; *p != 0; p++) {
		if (*p == ' ') {
			x += interspace;
			continue;
		}
		w = car(*p)[0];
		AffMask(*p, x, y, ptrfont);
		x += interleave + w;
	}
}
