#include <stddef.h>
#include <stdint.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: the port's own texts, numbered from 0x2000 past the sys
 * file's, in the game's language: Widescreen display's, the option
 * menu's switch of the wide screen. The port's own.
 */

/* the game's language, message.c's */
extern LONG	Language;

/*
 * TabPortText: two texts a language, in Language's order and in CP437,
 * Widescreen display's 0x2000 ON and 0x2001 OFF, said as the Classic's
 * Fullscreen display is.
 */
static char	*TabPortText[5*2] = {
	[0*2 + 0] = "Widescreen display: ON",
	[0*2 + 1] = "Widescreen display: OFF",
	[1*2 + 0] = "Affichage \202cran large: ON",
	[1*2 + 1] = "Affichage \202cran large: OFF",
	[2*2 + 0] = "Breitbildanzeige: ON",
	[2*2 + 1] = "Breitbildanzeige: OFF",
	[3*2 + 0] = "Pantalla panor\240mica: ON",
	[3*2 + 1] = "Pantalla panor\240mica: OFF",
	[4*2 + 0] = "Schermo panoramico: ON",
	[4*2 + 1] = "Schermo panoramico: OFF",
};

/* GetPortText: text num of TabPortText, its 0x2000 cleared, in the game's language. */
char*
GetPortText(int32_t num)
{
	return TabPortText[(num & ~0x2000) + Language*2];
}
