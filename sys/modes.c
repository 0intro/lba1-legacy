#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * FEATURE: the music's sources, lba.cfg's MusicTracks for the tunes the
 * game asks of the CD and MusicJingles for those it asks of the XMIDI,
 * each a word among the names of its modes, in the order of their
 * numbers, Auto last and the default: the first of the part's sources
 * the CD holds, taken once at the start. The port's own.
 */

int32_t	MusicTracks;
int32_t	MusicJingles;

char	*tracknames[] = {"CD", "Midi", "Classic", "Auto", NULL};
char	*jinglenames[] = {"Midi", "PlayStation", "CD", "Classic", "Auto", NULL};

/*
 * A source Auto takes, its mode, where the CD holds its file, the one
 * the mode plays for the first tune of its part (tune 1 for the tracks,
 * tune 0 for the jingles), NULL for the XMIDI, always there.
 */
typedef struct Source Source;
struct Source
{
	int32_t	mode;
	char	*file;
};

/*
 * The tracks' sources in Auto's order: the Classic's files, MP3 or
 * DotEmu's OGG, the CD's tracks, the XMIDI.
 */
static Source tracksources[] = {
	{Tclassic, "D:\\Common\\Music\\Track_02.mp3"},
	{Tclassic, "D:\\Common\\Music\\Track_02.ogg"},
	{Tcd, "D:\\TRACK02.WAV"},
	{Tmidi, NULL},
};

/*
 * The jingles' sources in Auto's order: the Classic's files, the cuts
 * from the CD's tracks (tune 0's from track 8), the PlayStation's
 * recordings (tune 0's J08MID), the XMIDI.
 */
static Source jinglesources[] = {
	{Jclassic, "D:\\Common\\Midi\\LBA1-01.mp3"},
	{Jclassic, "D:\\Common\\Midi\\LBA1-01.ogg"},
	{Jcd, "D:\\TRACK08.WAV"},
	{Jplaystation, "D:\\JINGLES\\J08MID.WAV"},
	{Jmidi, NULL},
};

/* A part of the music: its key, its modes' names, Auto's mode and its sources */
typedef struct Part Part;
struct Part
{
	char	*key;
	char	**names;
	int32_t	any;
	Source	*sources;
};

static Part parts[] = {
	{"MusicTracks", tracknames, Tauto, tracksources},
	{"MusicJingles", jinglenames, Jauto, jinglesources},
};

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

static Part*
part(char *key)
{
	Part *p;

	for (p = parts; p < parts + nelem(parts); p++)
		if (strcmp(p->key, key) == 0)
			return p;
	fatal("no music's part %s", key);
	return NULL;
}

/*
 * Whether the CD holds file where the players look for it: a host's
 * file of one byte or more, or an image's sound, the one file of an
 * image they read.
 */
static int
holds(char *file)
{
	char path[1024];
	Image *img;
	int32_t num;
	uint32_t first, n;

	if (readpath(file, path, sizeof path) == 0)
		return FileSize(file) != 0;
	return cdtrack(file, &img, &num, &first, &n) == 0;
}

/* automode: the mode Auto takes for key's part, the first source the CD holds. */
int32_t
automode(char *key)
{
	Source *s;

	for (s = part(key)->sources; s->file != NULL; s++)
		if (holds(s->file))
			break;
	return s->mode;
}

/*
 * musicmode: the mode key gives in file, as readmode reads it, and Auto,
 * the default, where the key is missing or empty, and with a line saying
 * so for a value none of its names: Auto's taken among its sources, with
 * a line.
 */
int32_t
musicmode(char *file, char *key)
{
	Part *p;
	char *s;
	int m, i;

	p = part(key);
	s = (char*)Def_ReadString((unsigned char*)file, (unsigned char*)key);
	m = s == NULL || *s == '\0' ? p->any : readmode(file, key, p->names);
	if (m < 0) {
		fprintf(stderr, "lba: %s: %s %s: none of %s", file, key, s, p->names[0]);
		for (i = 1; p->names[i] != NULL; i++)
			fprintf(stderr, "%s%s", p->names[i+1] == NULL ? " or " : ", ", p->names[i]);
		fprintf(stderr, ", %s taken\n", p->names[p->any]);
		m = p->any;
	}
	if (m == p->any) {
		m = automode(key);
		fprintf(stderr, "lba: %s Auto: %s\n", key, p->names[m]);
	}
	return m;
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
	 * FEATURE: the music's sources, the CD's tunes from MusicTracks and
	 * the jingles from MusicJingles. The port's own.
	 */
	MusicTracks = musicmode(name, "MusicTracks");
	MusicJingles = musicmode(name, "MusicJingles");
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
	/*
	 * FEATURE: GamePad, the Classic 3.2.3's input with ON, the keys read
	 * through the bindings of lba.cfg's Input keys, and the 1994
	 * keyboard's alone with OFF. The switch the port's own.
	 */
	FlagGamePad = onoff(name, "GamePad", 1);
}
