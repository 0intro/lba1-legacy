#include <stddef.h>
#include <stdint.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: the Classic's own texts, numbered from 0x1000 past the sys
 * file's, in the game's language: those of Hit walls, the option menu's
 * switch of the bump into walls. Taken from the Classic 3.2.3.
 */

/* the game's, which c_extern.h does not declare */
extern LONG	Language;

/*
 * TabExtText is TLBA1C.exe 0x7f2018: 128 texts a language, in
 * Language's order, in CP437. The port holds Hit walls' two, 0x100A ON
 * and 0x100B OFF.
 */
char	*TabExtText[5*128] = {
	[0*128 + 10] = "Hit walls: ON",
	[0*128 + 11] = "Hit walls: OFF",
	[1*128 + 10] = "Heurter les murs: ON",
	[1*128 + 11] = "Heurter les murs: OFF",
	[2*128 + 10] = "W\204nde treffen: ON",
	[2*128 + 11] = "W\204nde treffen: OFF",
	[3*128 + 10] = "Golpear paredes: ON",
	[3*128 + 11] = "Golpear paredes: OFF",
	[4*128 + 10] = "Colpisci i muri: ON",
	[4*128 + 11] = "Colpisci i muri: OFF",
};

/* GetExtText is TLBA1C.exe 0x40f140: text num, its 0x1000 cleared, in the game's language. */
char*
GetExtText(int32_t num)
{
	return TabExtText[(num & ~0x1000) + Language*128];
}
