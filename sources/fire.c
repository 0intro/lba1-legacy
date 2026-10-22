#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * The fire behind the menus' choices, of fire.asm: two images of 320x50
 * at the start of BufSpeak, Imagen, the fire, and the next step. Its
 * fuel is in the bottom lines: there a point cooled below 0 turns 255.
 */

UBYTE	*Imagen;

static uint8_t *imagen2;

/*
 * ComputeFire is fire.asm's ComputeFire: each point of the next step the
 * mean of its eight neighbours in the fire, less 1 when their sum is a
 * multiple of 4 and the mean is not 0, or whatever it is from line 46
 * on. The step, a line up, becomes the fire, then the step's lines 39 to
 * 43, where below 15, turn to 11 less themselves.
 */
static void
ComputeFire(void)
{
	uint8_t *s, *d, v;
	unsigned sum;
	int i;

	Imagen = BufSpeak;
	imagen2 = BufSpeak + 320*50;
	s = Imagen + 321;
	d = imagen2 + 321;
	for (i = 0; i < 48*320-2; i++, s++, d++) {
		sum = s[-321] + s[-320] + s[-319] + s[-1] + s[1] + s[319] + s[320] + s[321];
		v = sum >> 3;
		if ((sum & 3) == 0 && (321+i >= 46*320 || v != 0))
			v--;
		*d = v;
	}
	memcpy(Imagen, imagen2 + 320, 48*320);
	d = imagen2 + 39*320;
	for (i = 0; i < 5*320; i++, d++)
		if (*d < 15)
			*d = 11 - *d;
}

/* DoFire is fire.asm's DoFire: a step of the fire, its lines 5 to 29 doubled at line lig of Log, as half their heat above col, up to col+15. */
void
DoFire(int32_t lig, int32_t col)
{
	uint8_t *s, *d, c, top, v;
	int x, y;

	ComputeFire();
	s = Imagen + 5*320;
	d = Log + TabOffLine[lig];
	c = col;
	top = c + 15;
	for (y = 0; y < 25; y++) {
		for (x = 0; x < 320; x++) {
			v = (*s++ >> 1) + c;
			if (v > top)
				v = top;
			d[0] = d[1] = d[Screen_X] = d[Screen_X+1] = v;
			d += 2;
		}
		d += 2*Screen_X - 640;
	}
}
