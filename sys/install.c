#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "dat.h"
#include "fns.h"

/*
 * The install the port starts in, as a player starts it from the
 * install's folder: the current directory, else the program's. An
 * install is known by the files the port reads from it, and gives the
 * game's directories and its CD as binds (files.c): the CD itself, the
 * Classic's folder, DotEmu's Enhanced Edition, its files at the paths
 * it reads them from, or a DOS install, LbaWin's and DotEmu's DOS copy
 * among them. A DOS install's CD is its parent holding CD_LBA, as the
 * CD's LBA directory has it, else a cue sheet or an ISO beside it, as
 * the DOS copy has LBA.DAT, else a CD-ROM drive's, as LBA.EXE's InitCDR
 * asked MSCDEX for the drive holding CD_LBA. Where no install is found,
 * a CD's image in the folder (wholeimage), else the first of -c's CDs
 * that holds the CD's LBA directory (wholecd), else without -c a CD-ROM
 * drive's CD, is played whole: that directory bound as the game's and
 * the image, the CDs or the drive as the CD. An install's own saves are
 * bound at the root after its files: LbaWin's Save, the Classic's Save
 * and its folder in the user's Saved Games after it, DotEmu's saves.
 * The game lists and loads them, and writes every save into the
 * preferences. A record and its replay leave the drives (setdrives) and
 * the user's folders out, so that a record plays to its input alone on
 * every system.
 */

enum
{
	Npath = 1024,
	Nline = 1024,
	Nmark = 3,
};

/* How an install's CD is found */
enum
{
	Cdrows,		/* its rows' */
	Cdsearch,	/* its parent's, an image beside it, a drive's */
};

typedef struct Row Row;
typedef struct Install Install;

/* A bind an install makes: its directory dir bound at the game path path, on the CD's drive with cd set. */
struct Row
{
	int	cd;
	char	*path;
	char	*dir;	/* under the install's root, "." for the root */
};

/* An install the port knows by the files marker names under its root. */
struct Install
{
	char	*name;
	char	*marker[Nmark];
	Row	row[Nrow];
	int	cdfrom;
	char	*saved;	/* its saves' folder under the user's Saved Games, or NULL */
};

static int drives = 1;	/* the CD-ROM drives sought, 0 for the tests */

static Install installs[] = {
	{"the CD", {"CD_LBA", "LBA/RESS.HQR"},
		{{0, "", "LBA"}, {1, "", "."}}, Cdrows, NULL},
	{"the Classic", {"Common/RESS.HQR", "CommonClassic/TEXT.HQR"},
		{{0, "", "Common"}, {0, "", "CommonClassic"}, {0, "", "Save"}, {1, "", "."}}, Cdrows,
		"2point21/tlba-classic/Save"},
	{"the Enhanced Edition", {"resources/LBA_FILES/HQR/RESS.HQR"},
		{{0, "", "resources/LBA_FILES/HQR"},
		{0, "FLA", "resources/LBA_FILES/FLA"},
		{0, "FLA", "resources/LBA_FILES/HQR"},
		{0, "VOX", "resources/LBA_FILES/VOX/EN_VOICE"},
		{0, "VOX", "resources/LBA_FILES/VOX/FR_VOICE"},
		{0, "VOX", "resources/LBA_FILES/VOX/DE_VOICE"},
		{1, "Common/Midi", "resources/LBA_FILES/MIDI_MP3"},
		{1, "Common/Music", "resources/LBA_FILES/MUSIC"},
		{0, "", "saves"}}, Cdrows, NULL},
	{"a DOS install", {"RESS.HQR", "TEXT.HQR"},
		{{0, "", "."}, {0, "", "Save"}}, Cdsearch, NULL},
};

/* Whether root holds each of i's markers. */
static int
holds(char *root, Install *i)
{
	char path[Npath];
	int k;

	for (k = 0; k < Nmark && i->marker[k] != NULL; k++)
		if (findhost(root, i->marker[k], path, sizeof path) < 0)
			return 0;
	return 1;
}

/* Whether a file's name ends in ext, whatever its case. */
static int
endsin(char *name, char *ext)
{
	size_t n, e;

	n = strlen(name);
	e = strlen(ext);
	return n > e && watcomstricmp(name+n-e, ext) == 0;
}

/* Whether path is a CD's image whose data holds the file rel. */
static int
imagehas(char *path, char *rel)
{
	char err[256];
	uint32_t s, size;
	Image *m;
	int r;

	m = imageopen(path, err, sizeof err);
	if (m == NULL)
		return 0;
	r = imagefile(m, rel, &s, &size) == 0;
	imageclose(m);
	return r;
}

/* The cue sheet or ISO of dir whose data holds rel, the first by name, into best: 0, or -1 for none. */
static int
findimage(char *dir, char *rel, char *best, int n)
{
	char path[Npath];
	struct dirent *e;
	DIR *d;

	d = opendir(dir);
	if (d == NULL)
		return -1;
	best[0] = '\0';
	while ((e = readdir(d)) != NULL) {
		if (!endsin(e->d_name, ".cue") && !endsin(e->d_name, ".dat") && !endsin(e->d_name, ".iso"))
			continue;
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
			continue;
		if ((best[0] == '\0' || strcmp(path, best) < 0) && imagehas(path, rel))
			snprintf(best, n, "%s", path);
	}
	closedir(d);
	return best[0] == '\0' ? -1 : 0;
}

#ifdef _WIN32
/* The root of the first CD-ROM drive holding CD_LBA into out: 0, or -1. */
static int
cddrive(char *out, int n)
{
	char root[4], path[Npath];
	DWORD mask;
	UINT mode;
	int c, r;

	if (!drives)
		return -1;
	r = -1;
	mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
	mask = GetLogicalDrives();
	for (c = 'C'; c <= 'Z' && r < 0; c++) {
		if ((mask & ((DWORD)1 << (c-'A'))) == 0)
			continue;
		snprintf(root, sizeof root, "%c:\\", c);
		if (GetDriveTypeA(root) == DRIVE_CDROM && findhost(root, "CD_LBA", path, sizeof path) == 0) {
			snprintf(out, n, "%s", root);
			r = 0;
		}
	}
	SetErrorMode(mode);
	return r;
}
#else
/* s, a mount point as /proc/self/mounts writes it, its octal escapes (\040) undone. */
static void
unescape(char *s)
{
	char *d;

	for (d = s; *s != '\0'; d++) {
		if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] >= '0' && s[2] <= '7' && s[3] >= '0' && s[3] <= '7') {
			*d = (char)((s[1]-'0')<<6 | (s[2]-'0')<<3 | (s[3]-'0'));
			s += 4;
		} else
			*d = *s++;
	}
	*d = '\0';
}

/* The mount point of the first CD's file system (iso9660, udf) in mounts holding CD_LBA into out: 0, or -1. */
int
cdmount(char *mounts, char *out, int n)
{
	char line[Nline], dev[Nline], mnt[Nline], type[64], path[Npath];
	FILE *f;
	int r;

	f = fopen(mounts, "r");
	if (f == NULL)
		return -1;
	r = -1;
	while (r < 0 && fgets(line, sizeof line, f) != NULL) {
		if (sscanf(line, "%1023s %1023s %63s", dev, mnt, type) != 3)
			continue;
		if (strcmp(type, "iso9660") != 0 && strcmp(type, "udf") != 0)
			continue;
		unescape(mnt);
		if (findhost(mnt, "CD_LBA", path, sizeof path) == 0) {
			snprintf(out, n, "%s", mnt);
			r = 0;
		}
	}
	fclose(f);
	return r;
}

static int
cddrive(char *out, int n)
{
	if (!drives)
		return -1;
	return cdmount("/proc/self/mounts", out, n);
}
#endif

/* A bind's failure said, and the program ended. */
static void
cannot(char *what, char *err)
{
	fatal("%s: %s", what, err);
}

/*
 * A DOS install's CD bound: its parent holding CD_LBA, else an image
 * beside it, else a CD-ROM drive's, else its parent, as -c's default.
 */
static void
searchcd(char *root)
{
	char cd[Npath], path[Npath], err[256];

	snprintf(cd, sizeof cd, "%s/..", root);
	if (findhost(root, "../CD_LBA", path, sizeof path) < 0) {
		if (findimage(root, "CD_LBA", path, sizeof path) == 0) {
			if (bindpath(1, "", path, err, sizeof err) < 0)
				cannot(path, err);
			return;
		}
		if (cddrive(path, sizeof path) == 0)
			snprintf(cd, sizeof cd, "%s", path);
	}
	if (bindpath(1, "", cd, err, sizeof err) < 0)
		cannot(cd, err);
}

/* i's binds under root and saved, the user's Saved Games, the ncd CDs of cd in place of its CD's where given. */
static void
bindinstall(Install *i, char *root, char **cd, int ncd, const char *saved)
{
	char path[Npath], err[256];
	Row *r;

	for (r = i->row; r < i->row + Nrow && r->path != NULL; r++) {
		if (r->cd && ncd > 0)
			continue;
		if (findhost(root, r->dir, path, sizeof path) < 0)
			continue;
		if (bindpath(r->cd, r->path, path, err, sizeof err) < 0)
			cannot(path, err);
	}
	if (saved != NULL && i->saved != NULL && findhost(saved, i->saved, path, sizeof path) == 0) {
		if (bindpath(0, "", path, err, sizeof err) < 0)
			cannot(path, err);
	}
	if (ncd > 0)
		bindcds(cd, ncd);
	else if (i->cdfrom == Cdsearch)
		searchcd(root);
}

/*
 * The CD's image of root played whole, root holding no install: its LBA
 * directory bound as the game's, the image as the CD, or with CDs given
 * those in its place. 0, or -1 for none.
 */
static int
wholeimage(char *root, char **cd, int ncd)
{
	char path[Npath], err[256];

	if (findimage(root, "LBA/RESS.HQR", path, sizeof path) < 0)
		return -1;
	if (bindimage(0, "", path, "LBA", err, sizeof err) < 0)
		cannot(path, err);
	if (ncd > 0)
		bindcds(cd, ncd);
	else if (bindpath(1, "", path, err, sizeof err) < 0)
		cannot(path, err);
	return 0;
}

/* The install root holds, bound: its name, or NULL. */
static char*
recognize(char *root, char **cd, int ncd, const char *saved)
{
	Install *i;

	for (i = installs; i < installs + nelem(installs); i++)
		if (holds(root, i)) {
			bindinstall(i, root, cd, ncd, saved);
			return i->name;
		}
	if (wholeimage(root, cd, ncd) == 0)
		return "the CD's image";
	return NULL;
}

/* setdrives: whether the CD-ROM drives are sought, as by default, or left out, as the tests do. */
void
setdrives(int on)
{
	drives = on;
}

/*
 * The first of the ncd CDs of cd, a directory or an image, that holds
 * the CD's LBA directory, played whole: that directory bound as the
 * game's, then the CDs. Its index in cd, or -1 for none.
 */
static int
wholecd(char **cd, int ncd)
{
	char path[Npath], err[256];
	int i;

	for (i = 0; i < ncd; i++) {
		if (findhost(cd[i], "LBA/RESS.HQR", path, sizeof path) == 0 && findhost(cd[i], "LBA", path, sizeof path) == 0) {
			if (bindpath(0, "", path, err, sizeof err) < 0)
				cannot(path, err);
			break;
		}
		if (imagehas(cd[i], "LBA/RESS.HQR")) {
			if (bindimage(0, "", cd[i], "LBA", err, sizeof err) < 0)
				cannot(cd[i], err);
			break;
		}
	}
	if (i == ncd)
		return -1;
	bindcds(cd, ncd);
	return i;
}

/*
 * startin: the binds of the install the port starts in, the current
 * directory's, else prog's, the program's directory (NULL for none), the
 * ncd CDs of cd in place of its CD where given, its saves in saved, the
 * user's Saved Games (NULL for none), among them, and the install's
 * name, said on standard error. With none found, the first of the ncd
 * CDs that holds the CD's LBA directory plays whole (wholecd), else
 * with ncd 0 a CD-ROM drive's CD, else the current directory is bound
 * as -g . binds it, and as the CD the ncd CDs, or with ncd 0 its parent
 * as -c .. binds it, and the name is NULL.
 */
char*
startin(char *pref, const char *prog, char **cd, int ncd, const char *saved)
{
	char root[Npath], path[Npath], lba[Npath], err[256];
	char *name;
	int i;

	setpref(pref);
	snprintf(root, sizeof root, ".");
	name = recognize(root, cd, ncd, saved);
	if (name == NULL && prog != NULL) {
		snprintf(root, sizeof root, "%s", prog);
		name = recognize(root, cd, ncd, saved);
	}
	if (name != NULL) {
		fprintf(stderr, "lba: %s in %s\n", name, root);
		return name;
	}
	if (ncd > 0 && (i = wholecd(cd, ncd)) >= 0) {
		fprintf(stderr, "lba: the CD in %s\n", cd[i]);
		return "the CD";
	}
	if (ncd == 0 && cddrive(path, sizeof path) == 0 && findhost(path, "LBA", lba, sizeof lba) == 0) {
		if (bindpath(0, "", lba, err, sizeof err) < 0 || bindpath(1, "", path, err, sizeof err) < 0)
			cannot(path, err);
		fprintf(stderr, "lba: the CD in %s\n", path);
		return "the CD";
	}
	if (bindpath(0, "", ".", err, sizeof err) < 0 || (ncd == 0 && bindpath(1, "", "./..", err, sizeof err) < 0))
		cannot(".", err);
	bindcds(cd, ncd);
	return NULL;
}
