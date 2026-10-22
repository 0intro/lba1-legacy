#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "../sys/dat.h"
#include "../sys/dos.h"
#include "../sys/fns.h"

/*
 * installtest: the install the port finds where it starts
 * (sys/install.c), and where the game's files are read from through the
 * binds it makes (sys/files.c).
 *
 *	installtest dir trees
 *
 * Each case builds in dir a tree from trees/<list>, the names find
 * printed in a real tree, lowered for a CD in lower case, each file
 * holding its own path as its bytes, or from names of its own: an empty
 * folder, Common alone. A list's LBA.DAT is written as a cue sheet
 * naming LBA.DOT, a raw image of a small CD of the test's: CD_LBA and
 * LBA/FLA/INTROD.FLA in its data track, then an audio track 2, and
 * OTHER.ISO is that data track with CD_LBA under another name. The
 * search starts in a folder of the tree, the drives left out, the
 * user's Saved Games the tree's "Saved Games", and the case checks the
 * install found and the file each game path gives: its bytes, or none,
 * or one of the image, or the port's text of lba.cfg, which speaks
 * English, bound last as the program binds it, and the names a search
 * finds, the Classic's saves' whatever their folder. An image alone in
 * the folder is played whole. Cases of -c given more than once name CDs
 * under the tree, a directory and an image in either order, then eight
 * beside the Enhanced Edition's binds, and cases of -c alone an image
 * and a CD's directory, each played whole. Two cases start in the
 * install's own folder, the current directory, where the search looks
 * first: alone, and beside another install given as the program's. The
 * parser of /proc/self/mounts reads a file of its own, each path in it
 * escaped as the kernel escapes it.
 */

enum
{
	Npath = 1100,
	Nline = 1024,
	Sector = 2048,
	Raw = 2352,
	Nimage = 32,	/* the image's data sectors */
	Nframe = 10,	/* its audio track's frames */
	Nwant = 8,
};

typedef struct Want Want;
typedef struct Case Case;

/*
 * A game path and where it must be read from: a file of the tree, ""
 * for none, "@" in the CD's image, "=" the port's text.
 */
struct Want
{
	char	*path;
	char	*file;
};

struct Case
{
	char	*name;
	char	*list;	/* trees/list, or NULL for names */
	char	**names;
	int	lower;	/* the list's names in lower case */
	char	*start;	/* the folder searched, under the tree's root */
	char	*install;	/* the install found, NULL for none */
	Want	want[Nwant];
	char	*pattern;	/* a search, NULL for none, and the names it must find, a line each */
	char	*found;
};

static char *noname[] = {NULL};
static char *common[] = {"Common/", "Common/RESS.HQR", "Common/Fla/", "Common/Fla/FLASAMP.HQR", NULL};
static char *nobin[] = {"RESS.HQR", "TEXT.HQR", "LBA.DAT", NULL};
static char *otheriso[] = {"RESS.HQR", "TEXT.HQR", "OTHER.ISO", NULL};
static char *eesaves[] = {"resources/", "resources/LBA_FILES/", "resources/LBA_FILES/HQR/",
	"resources/LBA_FILES/HQR/RESS.HQR", "saves/", "saves/20200509154641.LBA", "saves/20200509154641.LBA.png", NULL};
static char *image[] = {"LBA.DAT", "LBA.DOT", NULL};
static char *classicsaves[] = {"Common/", "Common/RESS.HQR", "CommonClassic/", "CommonClassic/TEXT.HQR",
	"Save/", "Save/S1.LBA", "Saved Games/", "Saved Games/2point21/", "Saved Games/2point21/tlba-classic/",
	"Saved Games/2point21/tlba-classic/Save/", "Saved Games/2point21/tlba-classic/Save/S9271.LBA", NULL};

static Case cases[] = {
	{"eucd", "eucd", NULL, 0, "", "the CD",
		{{"RESS.HQR", "LBA/RESS.HQR"}, {"D:\\CD_LBA", "CD_LBA"}, {"D:\\LBA\\FLA\\INTROD.FLA", "LBA/FLA/INTROD.FLA"},
		{"FLA\\FLASAMP.HQR", "LBA/FLA/FLASAMP.HQR"}, {"LBA.CFG", "LBA/LBA.CFG"}}, NULL, NULL},
	{"eucdlba", "eucd", NULL, 0, "LBA", "a DOS install",
		{{"RESS.HQR", "LBA/RESS.HQR"}, {"D:\\CD_LBA", "CD_LBA"}, {"D:\\LBA\\VOX\\EN_000.VOX", "LBA/VOX/EN_000.VOX"}}, NULL, NULL},
	{"eucdlower", "eucd", NULL, 1, "", "the CD",
		{{"RESS.HQR", "lba/ress.hqr"}, {"D:\\CD_LBA", "cd_lba"}, {"TEXT.HQR", "lba/text.hqr"}}, NULL, NULL},
	{"nacd", "nacd", NULL, 0, "", "the CD",
		{{"RESS.HQR", "LBA/RESS.HQR"}, {"SETUP.LST", "LBA/SETUP.LST"}, {"D:\\CD_LBA", "CD_LBA"}}, NULL, NULL},
	{"dos", "dos", NULL, 0, "", "a DOS install",
		{{"RESS.HQR", "RESS.HQR"}, {"LBA.CFG", "LBA.CFG"}, {"VOX\\EN_000.VOX", "VOX/EN_000.VOX"}, {"D:\\CD_LBA", ""}}, NULL, NULL},
	{"lbawin", "lbawin", NULL, 0, "", "a DOS install",
		{{"RESS.HQR", "RESS.HQR"}, {"FLA\\FLASAMP.HQR", "FLA/FLASAMP.HQR"}, {"S1.LBA", "Save/S1.LBA"}, {"D:\\CD_LBA", ""}},
		"*.LBA", "S1.LBA\n"},
	{"gog", "gog", NULL, 0, "", "the Classic",
		{{"RESS.HQR", "Common/RESS.HQR"}, {"TEXT.HQR", "CommonClassic/TEXT.HQR"},
		{"FLA\\FLASAMP.HQR", "Common/Fla/FLASAMP.HQR"}, {"VOX\\EN_000.VOX", "Common/Vox/EN_000.VOX"},
		{"D:\\Common\\Music\\Track_02.mp3", "Common/Music/Track_02.mp3"}, {"SETUP.LST", ""}, {"D:\\CD_LBA", ""},
		{"LBA.CFG", "="}}, NULL, NULL},
	{"gogcopy", "gog", NULL, 0, "Speedrun/Windows", "a DOS install",
		{{"RESS.HQR", "Speedrun/Windows/RESS.HQR"}, {"SETUP.LST", "Speedrun/Windows/SETUP.LST"},
		{"D:\\CD_LBA", "@"}, {"D:\\LBA\\FLA\\INTROD.FLA", "@"}, {"D:\\TRACK02.WAV", "@"}, {"D:\\TRACK03.WAV", ""}}, NULL, NULL},
	{"gogcommon", "gog", NULL, 0, "Common", NULL,
		{{"RESS.HQR", ""}, {"FLA\\FLASAMP.HQR", ""}, {"LBA.CFG", "="}}, NULL, NULL},
	{"steam", "steam", NULL, 0, "", "the Classic",
		{{"RESS.HQR", "Common/RESS.HQR"}, {"TEXT.HQR", "CommonClassic/TEXT.HQR"}, {"D:\\CD_LBA", ""}}, NULL, NULL},
	{"steamee", "steam", NULL, 0, "DotEmu", "the Enhanced Edition",
		{{"RESS.HQR", "DotEmu/resources/LBA_FILES/HQR/RESS.HQR"},
		{"FLA\\FLASAMP.HQR", "DotEmu/resources/LBA_FILES/HQR/FLASAMP.HQR"},
		{"FLA\\INTROD.FLA", "DotEmu/resources/LBA_FILES/FLA/INTROD.FLA"},
		{"VOX\\FR_000.VOX", "DotEmu/resources/LBA_FILES/VOX/FR_VOICE/FR_000.VOX"},
		{"D:\\Common\\Music\\Track_02.mp3", "DotEmu/resources/LBA_FILES/MUSIC/Track_02.mp3"},
		{"D:\\Common\\Midi\\LBA1-01.mp3", "DotEmu/resources/LBA_FILES/MIDI_MP3/LBA1-01.mp3"},
		{"LBA.CFG", "="}, {"D:\\CD_LBA", ""}},
		"VOX\\*.VOX",
		"DE_000.VOX\nDE_001.VOX\nDE_002.VOX\nDE_003.VOX\nDE_004.VOX\nDE_005.VOX\n"
		"DE_006.VOX\nDE_007.VOX\nDE_008.VOX\nDE_009.VOX\nDE_010.VOX\nDE_GAM.VOX\n"
		"EN_000.VOX\nEN_001.VOX\nEN_002.VOX\nEN_003.VOX\nEN_004.VOX\nEN_005.VOX\n"
		"EN_006.VOX\nEN_007.VOX\nEN_008.VOX\nEN_009.VOX\nEN_010.VOX\nEN_GAM.VOX\n"
		"FR_000.VOX\nFR_001.VOX\nFR_002.VOX\nFR_003.VOX\nFR_004.VOX\nFR_005.VOX\n"
		"FR_006.VOX\nFR_007.VOX\nFR_008.VOX\nFR_009.VOX\nFR_010.VOX\nFR_GAM.VOX\n"},
	{"steamcopy", "steam", NULL, 0, "DotEmu/original", "a DOS install",
		{{"RESS.HQR", "DotEmu/original/RESS.HQR"}, {"D:\\CD_LBA", "@"}, {"VOX\\EN_000.VOX", "DotEmu/original/VOX/EN_000.VOX"}}, NULL, NULL},
	{"empty", NULL, noname, 0, "", NULL,
		{{"RESS.HQR", ""}, {"D:\\CD_LBA", ""}}, NULL, NULL},
	{"common", NULL, common, 0, "", NULL,
		{{"RESS.HQR", ""}, {"FLA\\FLASAMP.HQR", ""}}, NULL, NULL},
	{"nobin", NULL, nobin, 0, "", "a DOS install",
		{{"RESS.HQR", "RESS.HQR"}, {"D:\\CD_LBA", ""}}, NULL, NULL},
	{"otheriso", NULL, otheriso, 0, "", "a DOS install",
		{{"RESS.HQR", "RESS.HQR"}, {"D:\\CD_LBA", ""}, {"D:\\LBA\\FLA\\INTROD.FLA", ""}}, NULL, NULL},
	{"image", NULL, image, 0, "", "the CD's image",
		{{"RESS.HQR", "@"}, {"FLA\\INTROD.FLA", "@"}, {"D:\\CD_LBA", "@"}, {"D:\\LBA\\FLA\\INTROD.FLA", "@"},
		{"D:\\TRACK02.WAV", "@"}, {"LBA.CFG", "="}}, NULL, NULL},
	{"eesaves", NULL, eesaves, 0, "", "the Enhanced Edition",
		{{"20200509154641.LBA", "saves/20200509154641.LBA"}}, "*.LBA", "20200509154641.LBA\n"},
	{"classicsaves", NULL, classicsaves, 0, "", "the Classic",
		{{"S1.LBA", "Save/S1.LBA"}, {"S9271.LBA", "Saved Games/2point21/tlba-classic/Save/S9271.LBA"}},
		"*.LBA", "S1.LBA\nS9271.LBA\n"},
	{NULL, NULL, NULL, 0, NULL, NULL, {{NULL, NULL}}, NULL, NULL},
};

static char *cds[] = {"RESS.HQR", "TEXT.HQR", "LBA.DAT", "LBA.DOT", "cd/", "cd/CD_LBA", "cd/TRACK03.WAV", NULL};
static char *eecds[] = {"resources/", "resources/LBA_FILES/", "resources/LBA_FILES/HQR/",
	"resources/LBA_FILES/HQR/RESS.HQR", "resources/LBA_FILES/FLA/", "resources/LBA_FILES/VOX/",
	"resources/LBA_FILES/VOX/EN_VOICE/", "resources/LBA_FILES/VOX/FR_VOICE/", "resources/LBA_FILES/VOX/DE_VOICE/",
	"resources/LBA_FILES/MUSIC/", "resources/LBA_FILES/MUSIC/Track_02.mp3", "saves/",
	"c1/", "c2/", "c3/", "c4/", "c5/", "c6/", "c7/", "c8/", "c8/EIGHTH.TXT", NULL};
static char *alone[] = {"start/", "img/", "img/LBA.DAT", "img/LBA.DOT", NULL};
static char *cddir[] = {"start/", "cd/", "cd/CD_LBA", "cd/LBA/", "cd/LBA/RESS.HQR", NULL};

typedef struct Cdcase Cdcase;

/* A case whose CDs -c names, under the tree's root, in turn. */
struct Cdcase
{
	Case	c;
	char	*cd[Ncd];
};

static Cdcase cdcases[] = {
	{{"cds", NULL, cds, 0, "", "a DOS install",
		{{"RESS.HQR", "RESS.HQR"}, {"D:\\CD_LBA", "@"}, {"D:\\TRACK02.WAV", "@"},
		{"D:\\TRACK03.WAV", "cd/TRACK03.WAV"}}, NULL, NULL},
		{"LBA.DAT", "cd"}},
	{{"cdsswapped", NULL, cds, 0, "", "a DOS install",
		{{"D:\\CD_LBA", "cd/CD_LBA"}, {"D:\\LBA\\FLA\\INTROD.FLA", "@"},
		{"D:\\TRACK03.WAV", "cd/TRACK03.WAV"}}, NULL, NULL},
		{"cd", "LBA.DAT"}},
	{{"eecds", NULL, eecds, 0, "", "the Enhanced Edition",
		{{"RESS.HQR", "resources/LBA_FILES/HQR/RESS.HQR"}, {"D:\\Common\\Music\\Track_02.mp3", ""},
		{"D:\\EIGHTH.TXT", "c8/EIGHTH.TXT"}}, NULL, NULL},
		{"c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8"}},
	{{"whole", NULL, alone, 0, "start", "the CD",
		{{"RESS.HQR", "@"}, {"D:\\CD_LBA", "@"}, {"D:\\TRACK02.WAV", "@"}, {"LBA.CFG", "="}}, NULL, NULL},
		{"img/LBA.DAT"}},
	{{"wholedir", NULL, cddir, 0, "start", "the CD",
		{{"RESS.HQR", "cd/LBA/RESS.HQR"}, {"D:\\CD_LBA", "cd/CD_LBA"}, {"LBA.CFG", "="}}, NULL, NULL},
		{"cd"}},
	{{NULL, NULL, NULL, 0, NULL, NULL, {{NULL, NULL}}, NULL, NULL}, {NULL}},
};

static char cfg[] = "LBA.CFG";	/* the game's settings, bound last */
static char top[Npath], trees[Npath];
static char empty[Npath];	/* the current directory of the cases */
static int fails;
static uint8_t iso[Nimage*Sector];

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "installtest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

static void
fail(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "installtest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	if (++fails >= 20)
		fatal("too many failures");
}

/* A path made as snprintf makes it into out, fatal if it is cut short. */
static void
pathf(char *out, int n, char *fmt, ...)
{
	va_list arg;
	int k;

	va_start(arg, fmt);
	k = vsnprintf(out, n, fmt, arg);
	va_end(arg);
	if (k < 0 || k >= n)
		fatal("too long a path: %s", out);
}

static void
makedir(char *path)
{
#ifdef _WIN32
	_mkdir(path);
#else
	mkdir(path, 0777);
#endif
}

/* The host's current directory made path: 0, or -1. */
static int
gohost(char *path)
{
#ifdef _WIN32
	return _chdir(path);
#else
	return chdir(path);
#endif
}

static void
put(char *path, const void *b, size_t n)
{
	FILE *f;

	f = fopen(path, "wb");
	if (f == NULL || fwrite(b, 1, n, f) != n || fclose(f) != 0)
		fatal("writing %s", path);
}

static void
le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

/* A directory record at p: its length. */
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
 * The image's data track: the volume at 16, the root at 18 with CD_LBA.
 * and LBA, LBA at 19 with FLA and RESS.HQR, FLA at 20 with INTROD.FLA,
 * the files at 21, 22 and 23.
 */
static void
makeiso(void)
{
	uint8_t *p;

	p = iso + 16*Sector;
	p[0] = 1;
	memcpy(p+1, "CD001", 5);
	p[6] = 1;
	record(p+156, 18, Sector, 1, "\0", 1);
	p = iso + 18*Sector;
	p += record(p, 18, Sector, 1, "\0", 1);
	p += record(p, 18, Sector, 1, "\1", 1);
	p += record(p, 21, 6, 0, "CD_LBA.;1", 9);
	record(p, 19, Sector, 1, "LBA", 3);
	p = iso + 19*Sector;
	p += record(p, 19, Sector, 1, "\0", 1);
	p += record(p, 18, Sector, 1, "\1", 1);
	p += record(p, 20, Sector, 1, "FLA", 3);
	record(p, 23, 8, 0, "RESS.HQR;1", 10);
	p = iso + 20*Sector;
	p += record(p, 20, Sector, 1, "\0", 1);
	p += record(p, 19, Sector, 1, "\1", 1);
	record(p, 22, 10, 0, "INTROD.FLA;1", 12);
	memcpy(iso + 21*Sector, "CD_LBA", 6);
	memcpy(iso + 22*Sector, "INTROD.FLA", 10);
	memcpy(iso + 23*Sector, "RESS.HQR", 8);
}

/* The image as raw sectors at path, then its audio track with audio. */
static void
makebin(char *path, int audio)
{
	uint8_t raw[Raw];
	FILE *f;
	int s;

	f = fopen(path, "wb");
	if (f == NULL)
		fatal("writing %s", path);
	for (s = 0; s < Nimage; s++) {
		memset(raw, 0, sizeof raw);
		memcpy(raw+16, iso + s*Sector, Sector);
		if (fwrite(raw, 1, Raw, f) != Raw)
			fatal("writing %s", path);
	}
	memset(raw, 0x55, sizeof raw);
	for (s = 0; audio && s < Nframe; s++)
		if (fwrite(raw, 1, Raw, f) != Raw)
			fatal("writing %s", path);
	if (fclose(f) != 0)
		fatal("writing %s", path);
}

/* name, a tree's entry, made under root: a directory, or a file of its own path's bytes, an image for LBA.DAT and LBA.DOT. */
static void
make(char *root, char *name)
{
	static char cue[] = "FILE \"LBA.DOT\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
		"  TRACK 02 AUDIO\r\n    INDEX 01 00:00:32\r\n";
	char path[Npath], *base;
	size_t n;

	n = strlen(name);
	pathf(path, sizeof path, "%s/%s", root, name);
	if (n > 0 && name[n-1] == '/') {
		path[strlen(path)-1] = '\0';
		makedir(path);
		return;
	}
	base = strrchr(name, '/');
	base = base != NULL ? base+1 : name;
	if (strcmp(base, "LBA.DAT") == 0)
		put(path, cue, strlen(cue));
	else if (strcmp(base, "LBA.DOT") == 0)
		makebin(path, 1);
	else if (strcmp(base, "OTHER.ISO") == 0) {
		memcpy(iso + 18*Sector + 68 + 33, "CD_LBX", 6);
		put(path, iso, sizeof iso);
		memcpy(iso + 18*Sector + 68 + 33, "CD_LBA", 6);
	} else
		put(path, name, n);
}

/* The case's tree built at root. */
static void
build(Case *c, char *root)
{
	char path[Npath], line[Nline], *p;
	char **s;
	FILE *f;
	int n;

	makedir(root);
	if (c->list == NULL) {
		for (s = c->names; *s != NULL; s++)
			make(root, *s);
		return;
	}
	pathf(path, sizeof path, "%s/%s", trees, c->list);
	f = fopen(path, "r");
	if (f == NULL)
		fatal("no %s", path);
	while (fgets(line, sizeof line, f) != NULL) {
		n = strlen(line);
		while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
			line[--n] = '\0';
		if (n == 0 || line[0] == '#')
			continue;
		if (c->lower)
			for (p = line; *p != '\0'; p++)
				*p = tolower((unsigned char)*p);
		make(root, line);
	}
	fclose(f);
}

/*
 * The game's file at path read through the port into b, n bytes at
 * most, its kind in *own (1 for a file of the port's own, which has no
 * host's path): its bytes, 0 for a track of the CD's image, which the
 * port plays as sys/cd.c plays it, -1 where it opens none.
 */
static long
readall(char *path, char *b, unsigned n, int *own)
{
	char host[Npath];
	Image *img;
	uint32_t k, r, first, frames;
	int32_t h, track;

	*own = readpath(path, host, sizeof host) != 0;
	if ((h = OpenRead(path)) == 0)
		return *own && cdtrack(path, &img, &track, &first, &frames) == 0 ? 0 : -1;
	for (k = 0; k < n - 1 && (r = Read(h, b + k, n - 1 - k)) > 0; k += r)
		;
	b[k] = '\0';
	Close(h);
	return k;
}

/* Whether path, a game path, is read as want says. */
static void
check(char *name, Want *w)
{
	static char b[Npath];
	long k;
	int own;

	own = 0;
	k = readall(w->path, b, sizeof b, &own);
	if (w->file[0] == '\0') {
		if (k >= 0)
			fail("%s: %s found, want none", name, w->path);
	} else if (strcmp(w->file, "@") == 0) {
		if (k < 0 || !own)
			fail("%s: %s, want it in the CD's image", name, w->path);
	} else if (strcmp(w->file, "=") == 0) {
		if (k <= 0 || !own || strstr(b, "\r\nLanguage: English\r\n") == NULL
		|| strstr(b, "\r\nLanguageCD: English\r\n") == NULL)
			fail("%s: %s, want the port's text, which speaks English", name, w->path);
	} else if (k < 0 || own || strcmp(b, w->file) != 0)
		fail("%s: %s, want %s", name, w->path, w->file);
}

/* The case c run, the CDs of cd under its tree's root named as -c names them, NULL ending the list. */
static void
runcase(Case *c, char **cd)
{
	char root[Npath], start[Npath], pref[Npath], saved[Npath], cdpath[Ncd][Npath], *cds[Ncd], names[Nline];
	struct find_t ft;
	char *got;
	unsigned r;
	Want *w;
	size_t k;
	int ncd;

	pathf(root, sizeof root, "%s/%s", top, c->name);
	build(c, root);
	pathf(start, sizeof start, "%s/%s", root, c->start);
	pathf(pref, sizeof pref, "%s/pref-%s", top, c->name);
	pathf(saved, sizeof saved, "%s/Saved Games", root);
	makedir(pref);
	for (ncd = 0; ncd < Ncd && cd != NULL && cd[ncd] != NULL; ncd++) {
		pathf(cdpath[ncd], sizeof cdpath[ncd], "%s/%s", root, cd[ncd]);
		cds[ncd] = cdpath[ncd];
	}
	got = startin(pref, start, cds, ncd, saved);
	bindconfig(cfg, (char*)lbacfg);
	if ((got == NULL) != (c->install == NULL) || (got != NULL && strcmp(got, c->install) != 0))
		fail("%s: found %s, want %s", c->name, got != NULL ? got : "none", c->install != NULL ? c->install : "none");
	for (w = c->want; w < c->want + Nwant && w->path != NULL; w++)
		check(c->name, w);
	if (c->pattern != NULL) {
		names[0] = '\0';
		k = 0;
		for (r = _dos_findfirst(c->pattern, _A_NORMAL, &ft); r == 0 && k < sizeof names; r = _dos_findnext(&ft))
			k += (size_t)snprintf(names + k, sizeof names - k, "%s\n", ft.name);
		if (strcmp(names, c->found) != 0)
			fail("%s: %s finds \"%s\", want \"%s\"", c->name, c->pattern, names, c->found);
	}
}

/* The case of cases named name. */
static Case*
casenamed(char *name)
{
	Case *c;

	for (c = cases; c->name != NULL; c++)
		if (strcmp(c->name, name) == 0)
			return c;
	fatal("no case %s", name);
	return NULL;
}

/*
 * Case c started in its install's folder, the current directory, and
 * the install of other, if not NULL, given as the program's: c's
 * install found, its files read from it.
 */
static void
runcwd(Case *c, Case *other)
{
	char root[Npath], start[Npath], pref[Npath], ostart[Npath], name[Npath];
	char *got;
	Want *w;

	pathf(name, sizeof name, "%s, the current directory%s%s", c->name, other != NULL ? " beside " : "",
		other != NULL ? other->name : "");
	pathf(root, sizeof root, "%s/cwd-%s", top, c->name);
	build(c, root);
	pathf(start, sizeof start, "%s/%s", root, c->start);
	pathf(pref, sizeof pref, "%s/pref-cwd-%s", top, c->name);
	makedir(pref);
	if (other != NULL) {
		pathf(root, sizeof root, "%s/prog-%s", top, other->name);
		build(other, root);
		pathf(ostart, sizeof ostart, "%s/%s", root, other->start);
	}
	if (gohost(start) != 0)
		fatal("%s: no such directory", start);
	got = startin(pref, other != NULL ? ostart : NULL, NULL, 0, NULL);
	bindconfig(cfg, (char*)lbacfg);
	if (got == NULL || strcmp(got, c->install) != 0)
		fail("%s: found %s, want %s", name, got != NULL ? got : "none", c->install);
	for (w = c->want; w < c->want + Nwant && w->path != NULL; w++)
		check(name, w);
	if (gohost(empty) != 0)
		fatal("%s: no such directory", empty);
}

#ifndef _WIN32
/* s with the kernel's escapes of /proc/self/mounts, \040 for a space, \011, \012 and \134, in out. */
static void
mangle(char *out, int n, char *s)
{
	int k;

	for (k = 0; *s != '\0' && k+4 < n; s++) {
		if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\\')
			k += snprintf(out+k, n-k, "\\%03o", (unsigned char)*s);
		else
			out[k++] = *s;
	}
	out[k] = '\0';
	if (*s != '\0')
		fatal("%s: too long a path", s);
}

/* The mounts' parser: an iso9660 mount, its name escaped, holding CD_LBA, after one of ext4 holding it too and a udf one without. */
static void
mounts(void)
{
	char a[Npath], b[Npath], m[Npath], t[4*Npath], line[16*Npath], out[Npath];

	pathf(a, sizeof a, "%s/mnt/LBA \\CD", top);
	pathf(b, sizeof b, "%s/mnt/other", top);
	pathf(m, sizeof m, "%s/mnt", top);
	makedir(m);
	makedir(a);
	makedir(b);
	pathf(m, sizeof m, "%s/cd_lba", a);
	put(m, "x", 1);
	pathf(m, sizeof m, "%s/mnt/CD_LBA", top);
	put(m, "x", 1);
	mangle(t, sizeof t, top);
	pathf(line, sizeof line, "/dev/sda1 %s/mnt ext4 rw 0 0\n/dev/sr1 %s/mnt/other udf ro 0 0\n/dev/sr0 %s/mnt/LBA\\040\\134CD iso9660 ro 0 0\n",
		t, t, t);
	pathf(m, sizeof m, "%s/mounts", top);
	put(m, line, strlen(line));
	out[0] = '\0';
	if (cdmount(m, out, sizeof out) != 0 || strcmp(out, a) != 0)
		fail("mounts: %s, want %s", out, a);
}
#endif

int
main(int argc, char **argv)
{
	Case *c;
	Cdcase *d;

	if (argc != 3) {
		fprintf(stderr, "usage: installtest dir trees\n");
		return 2;
	}
	makedir(argv[1]);
	if (getcwd(top, sizeof top) == NULL)
		fatal("no current directory");
	if (argv[1][0] == '/' || (argv[1][0] != '\0' && argv[1][1] == ':'))
		snprintf(top, sizeof top, "%s", argv[1]);
	else
		snprintf(top + strlen(top), sizeof top - strlen(top), "/%s", argv[1]);
	if (argv[2][0] == '/' || (argv[2][0] != '\0' && argv[2][1] == ':'))
		snprintf(trees, sizeof trees, "%s", argv[2]);
	else {
		if (getcwd(trees, sizeof trees) == NULL)
			fatal("no current directory");
		snprintf(trees + strlen(trees), sizeof trees - strlen(trees), "/%s", argv[2]);
	}
	makeiso();
	pathf(empty, sizeof empty, "%s/empty", top);
	makedir(empty);
	if (gohost(empty) != 0)
		fatal("%s: no such directory", empty);
	setdrives(0);
	for (c = cases; c->name != NULL; c++)
		runcase(c, NULL);
	for (d = cdcases; d->c.name != NULL; d++)
		runcase(&d->c, d->cd);
	runcwd(casenamed("eucd"), NULL);
	runcwd(casenamed("gog"), casenamed("nobin"));
#ifndef _WIN32
	mounts();
#endif
	if (fails > 0)
		return 1;
	printf("installtest: ok\n");
	return 0;
}
