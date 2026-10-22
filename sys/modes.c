#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * readmode: the mode key gives in file, the index of its value among
 * names, the case ignored: 0 if the key is missing or empty, -1 for any
 * other value.
 */
int
readmode(char *file, char *key, char **names)
{
	char *s;
	int i;

	s = (char*)Def_ReadString((unsigned char*)file, (unsigned char*)key);
	if (s == NULL || *s == '\0')
		return 0;
	for (i = 0; names[i] != NULL; i++)
		if (watcomstricmp(s, names[i]) == 0)
			return i;
	return -1;
}

/*
 * readswitch: the switch key gives in file, 1 for ON and 0 for OFF, the
 * case ignored: dflt if the key is missing or empty, -1 for any other
 * value. The first name stands for the missing key.
 */
int
readswitch(char *file, char *key, int dflt)
{
	static char *names[] = {"", "OFF", "ON", NULL};
	int m;

	m = readmode(file, key, names);
	if (m == 0)
		return dflt;
	if (m < 0)
		return -1;
	return m == 2;
}

/*
 * The switch key in name, ON or OFF, as readswitch reads it: dflt where
 * the key is missing, and dflt with a line saying so for any other
 * value.
 */
static int32_t
onoff(char *name, char *key, int dflt)
{
	int v;

	v = readswitch(name, key, dflt);
	if (v >= 0)
		return v;
	fprintf(stderr, "lba: %s: %s %s: neither ON nor OFF, %s taken\n", name, key,
		(char*)Def_ReadString((unsigned char*)name, (unsigned char*)key), dflt ? "ON" : "OFF");
	return dflt;
}

typedef struct Gamekey Gamekey;

/* a switch of lba.cfg the game's own code reads, and its default */
struct Gamekey
{
	char	*key;
	int32_t	dflt;
};

/*
 * gamekeys: the switches of lba.cfg the game's own code reads
 * (sources/perso.c), each with its default, the value sys/lba.cfg holds
 * (cfgtest holds the two to each other)
 */
static Gamekey	gamekeys[] = {
	{"SaveAnywhere", 1},
	{"SkipLogo", 1},
};

/* gamekey: the switch key of gamekeys in the configuration file name, as onoff reads it with its default */
int32_t
gamekey(char *name, char *key)
{
	Gamekey *k;

	for (k = gamekeys; k < gamekeys + nelem(gamekeys); k++)
		if (strcmp(k->key, key) == 0)
			return onoff(name, key, k->dflt);
	fatal("no game's key %s", key);
	return 0;
}

/* gamekeyname: the key of gamekeys n, NULL past the last */
char*
gamekeyname(int n)
{
	if (n < 0 || n >= (int)nelem(gamekeys))
		return NULL;
	return gamekeys[n].key;
}

/*
 * readplatform: the port's keys of the configuration file name, read
 * once its places are set, each the value sys/lba.cfg holds where the
 * file leaves it out (cfgtest holds the two to each other), and
 * DisplaySync's into *sync, which the clock takes once it knows a
 * replay.
 */
void
readplatform(char *name, int *sync)
{
	/*
	 * FEATURE: LbaWin's settings, Hit walls among them, read once the
	 * configuration file's places are set, as the Classic 3.2.3 reads
	 * them after its InitConfigFile.
	 */
	ReadLbaWinSettings();
	/*
	 * FEATURE: the screen on the whole display or in a window, as
	 * LbaWinFullScreen last left it, the default kept when the key is
	 * missing, read after LbaWin's settings as the Classic 3.2.3's
	 * InitWindows (TLBA1C.exe 0x427a90) reads it.
	 */
	Def_ReadValue2((unsigned char*)name, (unsigned char*)"LbaWinFullScreen", &FlagFullScreen);
	/*
	 * FEATURE: the wide screen, lba.cfg's WideScreen: with ON, the
	 * default, the screen 480 lines high and as wide as the display's
	 * shape at the start, and 640x480 otherwise. The port's own.
	 */
	FlagWideScreen = onoff(name, "WideScreen", 1);
	/*
	 * FEATURE: DesiredDisplayNum, the display the window opens on, the
	 * nth of SDL's counted from 0, read where the Classic 3.2.3's
	 * InitSvga reads it (TLBA1C.exe 0x4043f0), before the wide screen
	 * takes its shape from that display. Taken from the Classic 3.2.3.
	 */
	Def_ReadValue2((unsigned char*)name, (unsigned char*)"DesiredDisplayNum", &DesiredDisplayNum);
	/*
	 * FEATURE: SmoothScaling, the screen scaled through SDL's linear
	 * filter with ON, the look of the Classic 3.2.3, which gives its
	 * texture that filter (TLBA1C.exe 0x406b70), and sharp with OFF.
	 * The switch the port's own.
	 */
	SetSmoothScaling(onoff(name, "SmoothScaling", 1));
	/*
	 * FEATURE: IntegerScaling, the screen scaled by whole multiples of
	 * itself with ON, and to the window's height or width with OFF.
	 * The port's own.
	 */
	FlagIntegerScaling = onoff(name, "IntegerScaling", 0);
	/*
	 * FEATURE: DisplaySync, each mode's frames paced by the display's
	 * refresh where n of its frames last VGA's frame of the mode within
	 * 1%, VGA's frame otherwise, and each present on the display's
	 * vertical sync with ON, as the Classic 3.2.3's renderer presents
	 * (TLBA1C.exe 0x406b70), and VGA's frames with the presents at that
	 * clock alone with OFF, set once the clock knows a replay
	 * (SetDisplaySync). vsync taken from the Classic 3.2.3, the pace the
	 * port's own, as LBA2's port paces its own.
	 */
	*sync = onoff(name, "DisplaySync", 1);
}
