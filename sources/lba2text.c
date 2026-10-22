#include <stddef.h>
#include <stdint.h>
#include "../sys/compat.h"
#include "c_extern.h"

/*
 * FEATURE: LBA2's own texts, numbered from 0x4000 past the sys file's
 * and the Classic's, in the game's language: those LBA2's main menu,
 * its lists of saves and its delete page show with SaveAnywhere on, and
 * Display text's on the language page. Taken from LBA2.
 */

/* the game's language, message.c's */
extern LONG	Language;

/*
 * TabLba2Text is LBA2's TEXT.HQR (the LBA2 Classic 3.2.4.3's Common/TEXT.HQR):
 * the sys file of its first five languages, in Language's order and in
 * CP437, by LBA2's numbers, 6 to 268, each text after the attribute byte
 * GetText drops. The port holds twelve: Display text's 16 and 17, the
 * delete page's 24 and 48, the lists' titles 41 and 42, and the main
 * menu's 70 to 75, 71 the lists' New Game line too.
 */
static char	*TabLba2Text[5*269] = {
	[0*269 + 16] = "Don't display text",
	[0*269 + 17] = "Display text",
	[0*269 + 24] = "Cancel",
	[0*269 + 41] = "Load game",
	[0*269 + 42] = "Type in your name",
	[0*269 + 48] = "Destroy",
	[0*269 + 70] = "Resume Game",
	[0*269 + 71] = "New Game",
	[0*269 + 72] = "Load Game",
	[0*269 + 73] = "Save Game",
	[0*269 + 74] = "Options Menu",
	[0*269 + 75] = "Quit",
	[1*269 + 16] = "Ne pas afficher les textes",
	[1*269 + 17] = "Afficher les textes",
	[1*269 + 24] = "Annuler",
	[1*269 + 41] = "Partie \205 charger",
	[1*269 + 42] = "Tapez votre Nom",
	[1*269 + 48] = "D\202truire",
	[1*269 + 70] = "Reprendre Partie",
	[1*269 + 71] = "Nouvelle Partie",
	[1*269 + 72] = "Charger une Partie",
	[1*269 + 73] = "Sauver la Partie",
	[1*269 + 74] = "Menu des Options",
	[1*269 + 75] = "Quitter",
	[2*269 + 16] = "Texte anzeigen AUS",
	[2*269 + 17] = "Texte anzeigen EIN",
	[2*269 + 24] = "Abbrechen",
	[2*269 + 41] = "Spiel laden",
	[2*269 + 42] = "Geben Sie einen Namen ein:",
	[2*269 + 48] = "L\224schen",
	[2*269 + 70] = "Spiel wiederaufnehmen",
	[2*269 + 71] = "Neues Spiel",
	[2*269 + 72] = "Spiel laden",
	[2*269 + 73] = "Spiel speichern",
	[2*269 + 74] = "Optionen",
	[2*269 + 75] = "Beenden",
	[3*269 + 16] = "No mostraros textos",
	[3*269 + 17] = "Mostrar textos",
	[3*269 + 24] = "Cancelar",
	[3*269 + 41] = "Cargar partida",
	[3*269 + 42] = "Escriba su nombre",
	[3*269 + 48] = "Destruir",
	[3*269 + 70] = "Continuar Partida",
	[3*269 + 71] = "Nueva Partida",
	[3*269 + 72] = "Cargar una partida",
	[3*269 + 73] = "Guardar partida",
	[3*269 + 74] = "Men\243 de Opciones",
	[3*269 + 75] = "Salir",
	[4*269 + 16] = "Visualizzare i testi NO",
	[4*269 + 17] = "Visualizzare i testi SI",
	[4*269 + 24] = "Annullare",
	[4*269 + 41] = "Partita da lanciare",
	[4*269 + 42] = "Scrivi il tuo nome",
	[4*269 + 48] = "Distruggere",
	[4*269 + 70] = "Riprendere la partita",
	[4*269 + 71] = "Nuova partita",
	[4*269 + 72] = "Caricare una partita",
	[4*269 + 73] = "Salvare la partita",
	[4*269 + 74] = "Men\227 delle opzioni",
	[4*269 + 75] = "Abbandonare",
};

/* GetLba2Text: text num of TabLba2Text, its 0x4000 cleared, in the game's language. */
char*
GetLba2Text(int32_t num)
{
	return TabLba2Text[(num & ~0x4000) + Language*269];
}
