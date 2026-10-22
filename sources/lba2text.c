#include <stddef.h>
#include <stdint.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: LBA2's own texts, numbered from 0x4000 past the sys file's
 * and the Classic's, in the game's language: Display text's on the
 * language page. Taken from LBA2.
 */

/* the game's language, message.c's */
extern LONG	Language;

/*
 * TabLba2Text is LBA2's TEXT.HQR (the LBA2 Classic 3.2.4.3's Common/TEXT.HQR):
 * the sys file of its first five languages, in Language's order and in
 * CP437, by LBA2's numbers, 6 to 268, each text after the attribute byte
 * GetText drops. The port holds two: Display text's 16 and 17.
 */
static char	*TabLba2Text[5*269] = {
	[0*269 + 16] = "Don't display text",
	[0*269 + 17] = "Display text",
	[1*269 + 16] = "Ne pas afficher les textes",
	[1*269 + 17] = "Afficher les textes",
	[2*269 + 16] = "Texte anzeigen AUS",
	[2*269 + 17] = "Texte anzeigen EIN",
	[3*269 + 16] = "No mostraros textos",
	[3*269 + 17] = "Mostrar textos",
	[4*269 + 16] = "Visualizzare i testi NO",
	[4*269 + 17] = "Visualizzare i testi SI",
};

/* GetLba2Text: text num of TabLba2Text, its 0x4000 cleared, in the game's language. */
char*
GetLba2Text(int32_t num)
{
	return TabLba2Text[(num & ~0x4000) + Language*269];
}
