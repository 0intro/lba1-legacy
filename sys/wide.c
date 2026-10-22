#include <stddef.h>
#include <stdint.h>
#include "dat.h"
#include "fns.h"

/*
 * FEATURE: the wide screen, lba.cfg's WideScreen: the screen 480 lines
 * high and as wide as the display's shape, the scene filling it and the
 * interface keeping 640x480 in its middle. The port's own.
 */

enum
{
	Minw = 640,
	Maxw = 1704,	/* 32:9, LBA2's port's widest */
	Height = 480,
};

/*
 * The width of a screen 480 lines high of the shape of a display dw by
 * dh, rounded down to a multiple of 4, for the fillers' patterns test a
 * pixel's address for its parity, and held within 640 and 1704: 640
 * for a display of 4:3 or narrower, or of 0 by 0.
 */
int32_t
screenwidth(int dw, int dh)
{
	int64_t w;

	if (dw <= 0 || dh <= 0)
		return Minw;
	w = (int64_t)Height*dw / dh;
	if (w > Maxw)
		w = Maxw;
	w &= ~(int64_t)3;
	if (w < Minw)
		w = Minw;
	return (int32_t)w;
}
