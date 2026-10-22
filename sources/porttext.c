#include <stddef.h>
#include <stdint.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: the port's own texts, numbered from 0x2000 past the sys
 * file's, in the game's language: Widescreen display's, the option
 * menu's switch of the wide screen, and the controls page's refusal to
 * clear a key a pad's input needs. The port's own.
 */

enum
{
	Ntext = 3,	/* the texts a language */
};

/* the game's language, message.c's */
extern LONG	Language;

/*
 * TabPortText: Ntext texts a language, in Language's order and in CP437,
 * Widescreen display's 0x2000 ON and 0x2001 OFF, said as the Classic's
 * Fullscreen display is, and 0x2002 the controls page's refusal to
 * clear an input's last key while a pad's input is bound to it, said as
 * the Classic names the pad.
 */
static char	*TabPortText[5*Ntext] = {
	[0*Ntext + 0] = "Widescreen display: ON",
	[0*Ntext + 1] = "Widescreen display: OFF",
	[0*Ntext + 2] = "The gamepad's binding needs this key",
	[1*Ntext + 0] = "Affichage \202cran large: ON",
	[1*Ntext + 1] = "Affichage \202cran large: OFF",
	[1*Ntext + 2] = "L'assignation du gamepad a besoin de cette touche",
	[2*Ntext + 0] = "Breitbildanzeige: ON",
	[2*Ntext + 1] = "Breitbildanzeige: OFF",
	[2*Ntext + 2] = "Die Gamepad-Belegung braucht diese Taste",
	[3*Ntext + 0] = "Pantalla panor\240mica: ON",
	[3*Ntext + 1] = "Pantalla panor\240mica: OFF",
	[3*Ntext + 2] = "La asignaci\242n del gamepad necesita esta tecla",
	[4*Ntext + 0] = "Schermo panoramico: ON",
	[4*Ntext + 1] = "Schermo panoramico: OFF",
	[4*Ntext + 2] = "L'assegnazione del gamepad richiede questo tasto",
};

/* GetPortText: text num of TabPortText, its 0x2000 cleared, in the game's language. */
char*
GetPortText(int32_t num)
{
	return TabPortText[(num & ~0x2000) + Language*Ntext];
}
