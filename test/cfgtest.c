#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sys/compat.h"
#include "../lib386/lib_sys/adeline.h"
#include "../lib386/lib_sys/lib_sys.h"
#include "../sys/dat.h"

#undef main	/* compat.h's, which names the game's lbamain */

/*
 * cfgtest: lba.cfg as lib386/lib_sys/def_file.c reads and writes it,
 * through the port's files, the preferences read first.
 *
 *	cfgtest prefdir gamedir
 *
 * The keys of the game's LBA.CFG, in gamedir, then files of its own,
 * written in prefdir, with the cases the 1994 loops make, the odd ones
 * among them: a value taken for a key, an empty number taking the next
 * line's first word, the last byte of a file without a line's end lost,
 * and a key added to such a file lost on its last line. Then the lines
 * def_file.c's fixes take: a value led by a tab written over whole,
 * lines ended by LF or CR alone, a key written among them keeping the
 * lines after it, and lines longer than DefString, a key read past them
 * and a value kept to its first 255 bytes. Then LbaWin's settings, as
 * lib386/lib_sys/settings.c reads, forces and writes them, the
 * switches WideScreen, SmoothScaling, IntegerScaling, DisplaySync
 * and GamePad, each with its default, DesiredDisplayNum, and the
 * music's sources, MusicTracks and MusicJingles, as sys/modes.c reads
 * them, Auto's taken among the files a CD's directory holds, the
 * preferences' own here, and on a CD's image of its own, whose audio
 * tracks alone count. First, the port's keys as readplatform reads
 * them, and the game's own switches, SaveAnywhere and SkipLogo, as
 * gamekey reads them for sources/perso.c, from sys/lba.cfg, shipped
 * beside the program and held as its text, each named there, and from
 * the CD's LBA.CFG, which leaves them out: the same values, the code's
 * defaults being the shipped file's, the music's again on a CD whose
 * tracks make Auto another source.
 */

void	setdirs(char*, char**, int, char**, int);
int	bindtext(int, char*, const char*, char*, int);
int	readmode(char*, char*, char**);
int	readswitch(char*, char*, int);
int32_t	automode(char*);
void	readplatform(char*, int*);
char*	gamekeyname(int);
int32_t	musicmode(char*, char*);


char	PathConfigFile[260];	/* sys/sys.c's, which cfgtest leaves out */
int32_t	FlagWideScreen, FlagIntegerScaling, DesiredDisplayNum;	/* sys/video.c's */
int32_t	FlagFullScreen = 1;	/* sys/video.c's, TLBA1C.exe 0x818854's value */
int32_t	FlagGamePad;	/* sys/input.c's */
static int32_t	smooth;	/* SmoothScaling, as readplatform gave it */

static char *pref;
static int fails;

/*
 * A line of lba.cfg, the key read and its names, and the mode readmode
 * gives: the first for a missing key or an empty value, -1 for a value
 * none of the names
 */
static struct {
	char	*line;
	char	*key;
	char	**names;
	int	m;
} modes[] = {
	{"MusicTracks: CD\r\n", "MusicTracks", tracknames, 0},
	{"MusicTracks: Midi\r\n", "MusicTracks", tracknames, 1},
	{"musictracks: cd\r\n", "MusicTracks", tracknames, 0},
	{"MusicTracks: MIDI  \r\n", "MusicTracks", tracknames, 1},
	{"MusicTracks:\r\n", "MusicTracks", tracknames, 0},
	{"MusicTracks: \r\n", "MusicTracks", tracknames, 0},
	{"MusicTracks: Mid\r\n", "MusicTracks", tracknames, -1},
	{"MusicTracks: Midis\r\n", "MusicTracks", tracknames, -1},
	{"MusicTracks: CD Midi\r\n", "MusicTracks", tracknames, -1},
	{"MusicTracks: PlayStation\r\n", "MusicTracks", tracknames, -1},
	{"MusicJingles: Midi\r\n", "MusicTracks", tracknames, 0},	/* missing */
	{"MusicJingles: Midi\r\n", "MusicJingles", jinglenames, 0},
	{"MusicJingles: PlayStation\r\n", "MusicJingles", jinglenames, 1},
	{"MusicJingles: playstation\r\n", "MusicJingles", jinglenames, 1},
	{"MusicJingles:\r\n", "MusicJingles", jinglenames, 0},
	{"MusicJingles: PS\r\n", "MusicJingles", jinglenames, -1},
	{"MusicJingles: CD\r\n", "MusicJingles", jinglenames, -1},
	{"MusicTracks: Midi\r\n", "MusicJingles", jinglenames, 0},
	{"MusicTracks: Auto\r\n", "MusicTracks", tracknames, 2},
	{"MusicTracks: auto\r\n", "MusicTracks", tracknames, 2},
	{"MusicJingles: AUTO\r\n", "MusicJingles", jinglenames, 2},
	{"MusicJingles: Automatic\r\n", "MusicJingles", jinglenames, -1},
};

/*
 * A line of lba.cfg, the switch read and its default, and what
 * readswitch gives: 1 for ON, 0 for OFF, the default for a missing key
 * or an empty value, -1 for any other value
 */
static struct {
	char	*line;
	char	*key;
	int	dflt;
	int	v;
} switches[] = {
	{"WideScreen: ON\r\n", "WideScreen", 1, 1},
	{"widescreen: off\r\n", "WideScreen", 1, 0},
	{"WideScreen: on  \r\n", "WideScreen", 1, 1},
	{"SmoothScaling: OFF\r\n", "WideScreen", 1, 1},	/* missing */
	{"WideScreen:\r\n", "WideScreen", 1, 1},
	{"WideScreen: auto\r\n", "WideScreen", 1, -1},
	{"WideScreen: 960x540\r\n", "WideScreen", 1, -1},
	{"WideScreen: 1\r\n", "WideScreen", 1, -1},
	{"SmoothScaling: OFF\r\n", "SmoothScaling", 1, 0},
	{"smoothscaling: on\r\n", "SmoothScaling", 1, 1},
	{"SmoothScaling: Off  \r\n", "SmoothScaling", 1, 0},
	{"WideScreen: OFF\r\n", "SmoothScaling", 1, 1},	/* missing */
	{"SmoothScaling:\r\n", "SmoothScaling", 1, 1},
	{"SmoothScaling: \r\n", "SmoothScaling", 1, 1},
	{"SmoothScaling: linear\r\n", "SmoothScaling", 1, -1},
	{"SmoothScaling: ONE\r\n", "SmoothScaling", 1, -1},
	{"SmoothScaling: 0\r\n", "SmoothScaling", 1, -1},
	{"IntegerScaling: ON\r\n", "IntegerScaling", 0, 1},
	{"integerscaling: off\r\n", "IntegerScaling", 0, 0},
	{"SmoothScaling: ON\r\n", "IntegerScaling", 0, 0},	/* missing */
	{"IntegerScaling:\r\n", "IntegerScaling", 0, 0},
	{"IntegerScaling: 2\r\n", "IntegerScaling", 0, -1},
	{"DisplaySync: OFF\r\n", "DisplaySync", 1, 0},
	{"displaysync: on\r\n", "DisplaySync", 1, 1},
	{"IntegerScaling: OFF\r\n", "DisplaySync", 1, 1},	/* missing */
	{"DisplaySync:\r\n", "DisplaySync", 1, 1},
	{"DisplaySync: vsync\r\n", "DisplaySync", 1, -1},
	{"GamePad: OFF\r\n", "GamePad", 1, 0},
	{"gamepad: on\r\n", "GamePad", 1, 1},
	{"DisplaySync: ON\r\n", "GamePad", 1, 1},	/* missing */
	{"GamePad:\r\n", "GamePad", 1, 1},
	{"GamePad: pad\r\n", "GamePad", 1, -1},
};

/*
 * The files a CD's directory holds and the modes Auto takes there for the
 * tracks and the jingles: the CD's tracks, the PlayStation's recordings,
 * the XMIDI, in that order
 */
static struct {
	char	*files[4];
	int32_t	tracks, jingles;
} autos[] = {
	{{NULL}, 1, 0},	/* none: the XMIDI */
	{{"TRACK02.WAV"}, 0, 0},	/* the CD's track 2 alone */
	{{"JINGLES\\J08MID.WAV"}, 1, 1},	/* the PlayStation's recordings */
};

/* SetSmoothScaling is sys/video.c's: here the flag kept. */
int32_t
SetSmoothScaling(int32_t flag)
{
	smooth = flag;
	return flag;
}

/* fatal is sys/fns.h's, as sys/sdl.c has it. */
void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "cfgtest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(2);
}

static void
put(char *name, char *text)
{
	char path[1024];
	FILE *f;

	if (snprintf(path, sizeof path, "%s/%s", pref, name) >= (int)sizeof path)
		fatal("%s/%s: too long", pref, name);
	f = fopen(path, "wb");
	if (f == NULL || fputs(text, f) == EOF || fclose(f) != 0)
		fatal("cannot write %s", path);
}

/* The file of the preferences as it now is. */
static char*
get(char *name)
{
	static char buf[4096];
	char path[1024];
	FILE *f;
	size_t n;

	snprintf(path, sizeof path, "%s/%s", pref, name);
	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return buf;
}

static void
str(char *file, char *key, char *want)
{
	char *got;

	got = (char*)Def_ReadString((UBYTE*)file, (UBYTE*)key);
	if (got == NULL && want == NULL)
		return;
	if (got != NULL && want != NULL && strcmp(got, want) == 0)
		return;
	fprintf(stderr, "cfgtest: %s %s: \"%s\" where \"%s\" was due\n", file, key,
		got != NULL ? got : "(none)", want != NULL ? want : "(none)");
	fails++;
}

static void
val(char *file, char *key, int found, LONG want)
{
	LONG got;
	int r;

	got = 0;
	r = Def_ReadValue2((UBYTE*)file, (UBYTE*)key, &got) != 0;
	if (r == found && (!found || got == want))
		return;
	fprintf(stderr, "cfgtest: %s %s: %s %ld where %s %ld was due\n", file, key,
		r ? "found" : "not found", (long)got, found ? "found" : "not found", (long)want);
	fails++;
}

static void
text(char *name, char *want)
{
	char *s;

	s = get(name);
	if (s == NULL || strcmp(s, want) != 0) {
		fprintf(stderr, "cfgtest: %s holds \"%s\" where \"%s\" was due\n", name, s != NULL ? s : "(none)", want);
		fails++;
	}
}

static void
mode(int i)
{
	int m;

	put("M.CFG", modes[i].line);
	m = readmode("M.CFG", modes[i].key, modes[i].names);
	if (m == modes[i].m)
		return;
	fprintf(stderr, "cfgtest: %s in \"%.*s\": %d where %d was due\n", modes[i].key,
		(int)strcspn(modes[i].line, "\r\n"), modes[i].line, m, modes[i].m);
	fails++;
}

static void
onoff(int i)
{
	int v;

	put("O.CFG", switches[i].line);
	v = readswitch("O.CFG", switches[i].key, switches[i].dflt);
	if (v == switches[i].v)
		return;
	fprintf(stderr, "cfgtest: %s in \"%.*s\": %d where %d was due\n", switches[i].key,
		(int)strcspn(switches[i].line, "\r\n"), switches[i].line, v, switches[i].v);
	fails++;
}

/* name, a file of the CD's directory, made of a byte, or empty for a name led by -, or removed */
static void
cdfile(char *name, int make)
{
	LONG h;

	if (!make) {
		Delete(name);
		return;
	}
	h = OpenWrite(name);
	if (h == 0 || Write(h, "x", 1) != 1)
		fatal("cannot write %s", name);
	Close(h);
}

static void
autocase(int i)
{
	int32_t t, j;
	int k, n;

	n = (int)(sizeof autos[i].files / sizeof autos[i].files[0]);
	for (k = 0; k < n && autos[i].files[k] != NULL; k++)
		cdfile(autos[i].files[k], 1);
	t = automode("MusicTracks");
	j = automode("MusicJingles");
	for (k = 0; k < n && autos[i].files[k] != NULL; k++)
		cdfile(autos[i].files[k], 0);
	if (t == autos[i].tracks && j == autos[i].jingles)
		return;
	fprintf(stderr, "cfgtest: Auto on");
	for (k = 0; k < n && autos[i].files[k] != NULL; k++)
		fprintf(stderr, " %s", autos[i].files[k]);
	fprintf(stderr, ": tracks %ld, jingles %ld where %ld, %ld was due\n",
		(long)t, (long)j, (long)autos[i].tracks, (long)autos[i].jingles);
	fails++;
}

enum
{
	Sector = 2048,
	Raw = 2352,
	Nauto = 24,	/* the sectors of autoimage's data track */
};

static void
le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

/* A directory record at p, of the extent at s of size bytes, a directory with dir: its length. */
static int
record(uint8_t *p, uint32_t s, uint32_t size, int dir, char *name, int namelen)
{
	int len;

	len = 33 + namelen;
	len += len & 1;
	memset(p, 0, len);
	p[0] = len;
	le32(p+2, s);
	le32(p+10, size);
	p[25] = dir ? 2 : 0;
	p[32] = namelen;
	memcpy(p+33, name, namelen);
	return len;
}

/*
 * A CD's image in the preferences, its path given back: a data track
 * holding JINGLES\J08MID.WAV, the file of the first tune of the
 * PlayStation's recordings (the volume at sector 16, the root at 18,
 * JINGLES at 21, the file at 23), as auto.iso, or
 * with audio 1 or 2 as auto.cue over auto.bin, the data track's raw
 * sectors then audio track 2 alone, of 20 frames, and with audio 2
 * J08MID.WAV two of them from its second, as the PlayStation's disc
 * places its jingles in its audio tracks.
 */
static char*
autoimage(int audio)
{
	static uint8_t iso[Nauto*Sector];
	static char path[1024];
	char bin[1024];
	uint8_t raw[Raw], *p;
	FILE *f;
	int s;

	memset(iso, 0, sizeof iso);
	p = iso + 16*Sector;
	p[0] = 1;
	memcpy(p+1, "CD001", 5);
	p[6] = 1;
	record(p+156, 18, Sector, 1, "\0", 1);
	record(iso + 18*Sector, 21, Sector, 1, "JINGLES", 7);
	if (audio == 2)
		record(iso + 21*Sector, Nauto+1, 2*Sector, 0, "J08MID.WAV;1", 12);
	else
		record(iso + 21*Sector, 23, 1, 0, "J08MID.WAV;1", 12);
	iso[23*Sector] = 'x';
	snprintf(path, sizeof path, "%s/auto.iso", pref);
	if (!audio) {
		f = fopen(path, "wb");
		if (f == NULL || fwrite(iso, 1, sizeof iso, f) != sizeof iso || fclose(f) != 0)
			fatal("cannot write %s", path);
		return path;
	}
	snprintf(bin, sizeof bin, "%s/auto.bin", pref);
	f = fopen(bin, "wb");
	if (f == NULL)
		fatal("cannot write %s", bin);
	memset(raw, 0, sizeof raw);
	for (s = 0; s < Nauto+20; s++) {
		if (s < Nauto)
			memcpy(raw+16, iso + s*Sector, Sector);
		else
			memset(raw, 0, sizeof raw);
		if (fwrite(raw, 1, Raw, f) != Raw)
			fatal("cannot write %s", bin);
	}
	if (fclose(f) != 0)
		fatal("cannot write %s", bin);
	snprintf(path, sizeof path, "%s/auto.cue", pref);
	f = fopen(path, "wb");
	if (f == NULL || fprintf(f, "FILE \"auto.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"  TRACK 02 AUDIO\n    INDEX 01 00:00:%02d\n", Nauto) < 0
	|| fclose(f) != 0)
		fatal("cannot write %s", path);
	return path;
}

/*
 * Auto on the image autoimage writes as the CD: the modes due for the
 * tracks and the jingles, its sounds taken, its audio tracks and the
 * files its directory places in them, and its data track's files passed
 * over, which no player of the music reads.
 */
static void
autoimagecase(char **game, int audio, int32_t tracks, int32_t jingles)
{
	char *path, bin[1024];
	int32_t t, j;

	path = autoimage(audio);
	setdirs(pref, game, 1, &path, 1);
	t = automode("MusicTracks");
	j = automode("MusicJingles");
	setdirs(pref, game, 1, &pref, 1);
	snprintf(bin, sizeof bin, "%s/auto.bin", pref);
	remove(path);
	remove(bin);
	if (t == tracks && j == jingles)
		return;
	fprintf(stderr, "cfgtest: Auto on %s: tracks %ld, jingles %ld where %ld, %ld was due\n",
		path, (long)t, (long)j, (long)tracks, (long)jingles);
	fails++;
}

/*
 * Lines longer than DefString's 256 bytes: a comment before the keys, a
 * key's value and a number's word, each read to its first 255 bytes,
 * and the file written again whole past them.
 */
static void
longcase(void)
{
	char file[2048], want[2048], comment[301], value[301], word[301];

	memset(comment, 'x', sizeof comment - 1);
	comment[sizeof comment - 1] = 0;
	memset(value, 'v', sizeof value - 1);
	value[sizeof value - 1] = 0;
	memset(word, 'w', sizeof word - 1);
	word[sizeof word - 1] = 0;
	snprintf(file, sizeof file, "; %s\r\nAlpha: one\r\nLong: %s\r\nWord: %s\r\nNeg: -5\r\n",
		comment, value, word);
	put("G.CFG", file);
	str("G.CFG", "Alpha", "one");
	value[255] = 0;
	str("G.CFG", "Long", value);
	val("G.CFG", "Word", 1, 0);
	val("G.CFG", "Neg", 1, -5);
	Def_WriteValue((UBYTE*)"G.CFG", (UBYTE*)"Long", 7);
	snprintf(want, sizeof want, "; %s\r\nAlpha: one\r\nLong: 7\r\nWord: %s\r\nNeg: -5\r\n",
		comment, word);
	text("G.CFG", want);
}

/* The port's keys, in readplatform's order */
static char *names[] = {
	"LbaWinSettings", "LbaWinFullScreen", "WideScreen", "DesiredDisplayNum",
	"MusicTracks", "MusicJingles", "SmoothScaling", "IntegerScaling", "DisplaySync", "GamePad",
};

/* readplatform's values for the configuration file name into v, in names' order */
static void
platform(char *name, int32_t *v)
{
	int sync;

	strcpy(PathConfigFile, name);
	readplatform(name, &sync);
	v[0] = LbaWinSettings;
	v[1] = FlagFullScreen;
	v[2] = FlagWideScreen;
	v[3] = DesiredDisplayNum;
	v[4] = MusicTracks;
	v[5] = MusicJingles;
	v[6] = smooth;
	v[7] = FlagIntegerScaling;
	v[8] = sync;
	v[9] = FlagGamePad;
}

/* key named in sys/lba.cfg, and its value a where the CD's LBA.CFG leaves it out the same as b, sys/lba.cfg's */
static void
shipped(char *key, int32_t a, int32_t b)
{
	if (Def_ReadString((UBYTE*)"SHIPPED.CFG", (UBYTE*)key) == NULL) {
		fprintf(stderr, "cfgtest: sys/lba.cfg leaves %s out\n", key);
		fails++;
	}
	if (a != b) {
		fprintf(stderr, "cfgtest: %s left out: %ld where sys/lba.cfg gives %ld\n", key, (long)a, (long)b);
		fails++;
	}
}

/*
 * The port's keys of sys/lba.cfg, the program's text of it (lbacfg),
 * and of the CD's LBA.CFG, which leaves them out, read first, while
 * LbaWinSettings and the data the reads keep hold their values at the
 * start, then the game's own switches (gamekeys) as the game reads
 * them: each named in sys/lba.cfg, and the same in both.
 */
static void
defaults(void)
{
	char err[256];
	int32_t a[nelem(names)], b[nelem(names)];
	char *key;
	int i;

	platform("LBA.CFG", a);
	if (bindtext(0, "SHIPPED.CFG", (char*)lbacfg, err, sizeof err) < 0)
		fatal("SHIPPED.CFG: %s", err);
	platform("SHIPPED.CFG", b);
	for (i = 0; i < (int)nelem(names); i++)
		shipped(names[i], a[i], b[i]);
	for (i = 0; (key = gamekeyname(i)) != NULL; i++)
		shipped(key, gamekey("LBA.CFG", key), gamekey("SHIPPED.CFG", key));
}

/*
 * The music's sources of sys/lba.cfg and of the CD's LBA.CFG on a CD
 * whose track 2 makes Auto the CD's tracks: the same.
 */
static void
musicdefaults(void)
{
	char err[256];
	int32_t t, j;

	cdfile("TRACK02.WAV", 1);
	if (bindtext(0, "SHIPPED.CFG", (char*)lbacfg, err, sizeof err) < 0)
		fatal("SHIPPED.CFG: %s", err);
	t = musicmode("LBA.CFG", "MusicTracks");
	j = musicmode("LBA.CFG", "MusicJingles");
	if (t != musicmode("SHIPPED.CFG", "MusicTracks") || j != musicmode("SHIPPED.CFG", "MusicJingles")) {
		fprintf(stderr, "cfgtest: the music's sources left out, on the CD's tracks: %ld and %ld where sys/lba.cfg gives others\n",
			(long)t, (long)j);
		fails++;
	}
	cdfile("TRACK02.WAV", 0);
}

static void
setting(LONG want)
{
	if (LbaWinSettings == want)
		return;
	fprintf(stderr, "cfgtest: LbaWinSettings is %ld where %ld was due\n",
		(long)LbaWinSettings, (long)want);
	fails++;
}

int
main(int argc, char **argv)
{
	int i;

	if (argc != 3) {
		fprintf(stderr, "usage: cfgtest prefdir gamedir\n");
		return 2;
	}
	pref = argv[1];
	setdirs(argv[1], &argv[2], 1, &argv[2], 1);
	defaults();

	str("LBA.CFG", "Language", "English");
	str("LBA.CFG", "SvgaDriver", "VESA");
	str("LBA.CFG", "MidiDriver", "NoMidi");
	str("LBA.CFG", "MidiBase", "");
	val("LBA.CFG", "LbaWinSettings", 0, 0);
	str("LBA.CFG", "MusicTracks", NULL);
	str("LBA.CFG", "MusicJingles", NULL);
	val("LBA.CFG", "WaveRate", 1, 0);	/* empty: the next line's first word, read as 0 */

	put("T.CFG",
		"; a comment\r\n"
		"Alpha: one\r\n"
		"  beta:two words  \r\n"
		"Gamma :\t three\r\n"
		"Empty:\r\n"
		"Blank: \r\n"
		"Hex: 220h\r\n"
		"Dec: 22000\r\n"
		"Neg: -5\r\n");
	str("T.CFG", "Alpha", "one");
	str("T.CFG", "ALPHA", "one");
	str("T.CFG", "beta", "two words");
	str("T.CFG", "Gamma", "three");
	str("T.CFG", "Empty", "");
	str("T.CFG", "Blank", "");
	str("T.CFG", "Missing", NULL);
	str("T.CFG", "one", "");	/* Alpha's value, taken for a key */
	val("T.CFG", "Hex", 1, 0x220);
	val("T.CFG", "Dec", 1, 22000);
	val("T.CFG", "Neg", 1, -5);
	val("T.CFG", "Empty", 1, 0);	/* Blank:, the next line's */
	val("T.CFG", "Missing", 0, 0);
	put("A.CFG", "Big: 4294967295\r\nPast: 2147483648\r\nNegpast: -2147483649\r\n");
	val("A.CFG", "Big", 1, -1);	/* past 32 bits, modulo 2^32 as Watcom's atoi */
	val("A.CFG", "Past", 1, INT32_MIN);
	val("A.CFG", "Negpast", 1, INT32_MAX);
	Def_WriteString((UBYTE*)"T.CFG", (UBYTE*)"Dec", (UBYTE*)"11025");
	text("T.CFG",
		"; a comment\r\n"
		"Alpha: one\r\n"
		"  beta:two words  \r\n"
		"Gamma :\t three\r\n"
		"Empty:\r\n"
		"Blank: \r\n"
		"Hex: 220h\r\n"
		"Dec: 11025\r\n"
		"Neg: -5\r\n");
	/* a key that ends the file, its line's end missing */
	put("E.CFG", "One: 1\r\nLast:");
	Def_WriteString((UBYTE*)"E.CFG", (UBYTE*)"Last", (UBYTE*)"2");
	text("E.CFG", "One: 1\r\nLast: 2\r\n");
	Def_WriteValue((UBYTE*)"T.CFG", (UBYTE*)"New", 7);
	val("T.CFG", "New", 1, 7);
	val("T.CFG", "Dec", 1, 11025);
	Def_WriteValue((UBYTE*)"T.CFG", (UBYTE*)"Gamma", 4);	/* a value led by a tab */
	text("T.CFG",
		"; a comment\r\n"
		"Alpha: one\r\n"
		"  beta:two words  \r\n"
		"Gamma : 4\r\n"
		"Empty:\r\n"
		"Blank: \r\n"
		"Hex: 220h\r\n"
		"Dec: 11025\r\n"
		"Neg: -5\r\n"
		"New: 7\r\n");

	put("U.CFG", "Last: xy");
	str("U.CFG", "Last", "x");	/* the last byte lost */
	Def_WriteValue((UBYTE*)"U.CFG", (UBYTE*)"New", 7);
	text("U.CFG", "Last: xyNew: 7\r\n");
	str("U.CFG", "New", NULL);	/* added on the last line */
	put("U.CFG", "First: 1\r\nLast: xy");
	Def_WriteValue((UBYTE*)"U.CFG", (UBYTE*)"Last", 9);
	text("U.CFG", "First: 1\r\nLast: 9\r\n");	/* the old value to the file's end */

	put("L.CFG", "Alpha: one\nDec: 22000\nEmpty:\nNeg: -5\n");
	str("L.CFG", "Alpha", "one");
	val("L.CFG", "Dec", 1, 22000);
	val("L.CFG", "Neg", 1, -5);
	Def_WriteValue((UBYTE*)"L.CFG", (UBYTE*)"Dec", 11025);
	text("L.CFG", "Alpha: one\nDec: 11025\r\nEmpty:\nNeg: -5\n");
	Def_WriteValue((UBYTE*)"L.CFG", (UBYTE*)"Empty", 3);
	text("L.CFG", "Alpha: one\nDec: 11025\r\nEmpty: 3\r\nNeg: -5\n");
	val("L.CFG", "Neg", 1, -5);

	put("C.CFG", "Alpha: one\rDec: 22000\rNeg: -5\r");
	str("C.CFG", "Alpha", "one");
	val("C.CFG", "Dec", 1, 22000);
	Def_WriteValue((UBYTE*)"C.CFG", (UBYTE*)"Dec", 11025);
	text("C.CFG", "Alpha: one\rDec: 11025\r\nNeg: -5\r");

	longcase();

	strcpy(PathConfigFile, "S.CFG");
	put("S.CFG", "Other: 1\r\n");
	ReadLbaWinSettings();
	setting(212);	/* missing: the default */
	WriteLbaWinSettings();
	text("S.CFG", "Other: 1\r\nLbaWinSettings: 212\r\n");
	put("S.CFG", "LbaWinSettings: 209\r\n");
	ReadLbaWinSettings();
	setting(211);	/* LbaWin's: keep original game play sets Hit walls */
	put("S.CFG", "LbaWinSettings: 213\r\n");
	ReadLbaWinSettings();
	setting(211);	/* and clears the action key */
	put("S.CFG", "LbaWinSettings: 212\r\n");
	ReadLbaWinSettings();
	setting(212);	/* the Classic's: Hit walls off */
	put("S.CFG", "LbaWinSettings: 1\r\n");
	ReadLbaWinSettings();
	setting(19);	/* keep original game play sets the fades too */
	put("S.CFG", "LbaWinSettings: 197\r\n");
	ReadLbaWinSettings();
	setting(211);
	put("S.CFG", "LbaWinSettings: 194\r\n");
	ReadLbaWinSettings();
	setting(194);	/* the fades off */

	put("W.CFG", "WideScreen: ON\r\nOther: auto\r\n");
	str("W.CFG", "WideScreen", "ON");
	str("W.CFG", "Other", "auto");
	put("D.CFG", "DesiredDisplayNum: 1\r\n");
	val("D.CFG", "DesiredDisplayNum", 1, 1);
	val("LBA.CFG", "DesiredDisplayNum", 0, 0);	/* the CD's: SDL's primary */

	for (i = 0; i < (int)(sizeof modes / sizeof modes[0]); i++)
		mode(i);
	for (i = 0; i < (int)(sizeof switches / sizeof switches[0]); i++)
		onoff(i);

	/* the CD's directory the preferences, which the cases fill and empty */
	setdirs(argv[1], &argv[2], 1, &argv[1], 1);
	for (i = 0; i < (int)(sizeof autos / sizeof autos[0]); i++)
		autocase(i);
	/* the CD an image, as an ISO, then with audio track 2, then with J08MID.WAV in it */
	autoimagecase(&argv[2], 0, 1, 0);
	autoimagecase(&argv[2], 1, 0, 0);
	autoimagecase(&argv[2], 2, 0, 1);
	setdirs(argv[1], &argv[2], 1, &argv[1], 1);
	musicdefaults();

	if (fails != 0)
		return 1;
	printf("cfgtest: ok\n");
	return 0;
}
